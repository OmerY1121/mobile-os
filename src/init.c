#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_QUICK_FAILURES 3
#define STABLE_SECONDS 30

static void manage_statusd(void)
{
    int quick_failures = 0;

    for (;;) {
        time_t started = time(NULL);
        pid_t child = fork();

        if (child < 0) {
            perror("init: fork");
            sleep(2);
            continue;
        }

        if (child == 0) {
            execl("/bin/statusd", "statusd", (char *)NULL);
            perror("init: exec statusd");
            _exit(127);
        }

        printf("init: statusd started (PID %ld)\n",
               (long)child);

        int status;

        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR)
                continue;

            perror("init: waitpid");
            return;
        }

        if (WIFEXITED(status)) {
            printf("init: statusd exited with code %d\n",
                   WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("init: statusd stopped by signal %d\n",
                   WTERMSIG(status));
        }

        if (time(NULL) - started >= STABLE_SECONDS)
            quick_failures = 0;

        if (++quick_failures >= MAX_QUICK_FAILURES) {
            puts("init: statusd restart limit reached");
            break;
        }

        sleep(quick_failures);
    }
}

int main(void)
{
    if (getpid() != 1) {
        fputs("init must run as PID 1\n", stderr);
        return 1;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    puts("Mobile OS 0.0.3");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        perror("init: mount /proc");

    manage_statusd();

    for (;;)
        pause();
}
