#include <stdio.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int ready[2];

    if (pipe(ready) < 0) {
        perror("statusd: pipe");
        return 24;
    }

    pid_t orphan = fork();

    if (orphan < 0) {
        perror("statusd: fork");
        return 24;
    }

    if (orphan == 0) {
        close(ready[0]);

        struct sigaction action = {0};
        action.sa_handler = SIG_IGN;
        sigemptyset(&action.sa_mask);

        if (sigaction(SIGTERM, &action, NULL) < 0)
            _exit(24);

        char token = 'R';

        if (write(ready[1], &token, 1) != 1)
            _exit(24);

        close(ready[1]);

        for (;;)
            pause();
    }

    close(ready[1]);

    char token = 0;
    ssize_t bytes = read(ready[0], &token, 1);
    close(ready[0]);

    if (bytes != 1 || token != 'R') {
        fputs("statusd: orphan synchronization failed\n",
              stderr);
        return 24;
    }

    int nnp = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);

    printf(
        "statusd: injected crash UID=%ld PID=%ld no_new_privs=%d\n",
        (long)getuid(), (long)getpid(), nnp
    );

    return 23;
}
