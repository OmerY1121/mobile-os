#include <sys/stat.h>
#include <grp.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STATUSD_UID 1100
#define STATUSD_GID 1100

#define MAX_QUICK_FAILURES 3
#define STABLE_SECONDS 30

static int prepare_statusd_directory(void)
{
    if (chown("/run/mobile-os",
              STATUSD_UID, STATUSD_GID) < 0) {
        perror("init: chown status directory");
        return -1;
    }

    if (chmod("/run/mobile-os", 0700) < 0) {
        perror("init: chmod status directory");
        return -1;
    }

    return 0;
}

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
            if (setgroups(0, NULL) < 0 ||
                setgid(STATUSD_GID) < 0 ||
                setuid(STATUSD_UID) < 0) {
                perror("init: drop service privileges");
                _exit(126);
            }

            printf("init: statusd credentials UID=%ld GID=%ld\n",
                   (long)getuid(), (long)getgid());

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

    puts("Mobile OS 0.0.5.1");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        perror("init: mount /proc");

    if (prepare_statusd_directory() == 0)
        manage_statusd();
    else
        fputs("init: statusd not started\n", stderr);

    for (;;)
        pause();
}
