#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SELYRA_TEST_ACTION
#error "Set SELYRA_TEST_ACTION at compile time"
#endif

static int send_request(void)
{
    pid_t child = fork();
    if (child < 0) return -1;

    if (child == 0) {
        execl("/bin/powerctl", "powerctl",
              SELYRA_TEST_ACTION, (char *)NULL);
        perror("SEL-POWER-E2E: exec powerctl");
        _exit(127);
    }

    int status;

    for (;;) {
        if (waitpid(child, &status, 0) >= 0)
            break;
        if (errno != EINTR)
            return -1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(void)
{
    if (getpid() != 1) {
        fputs("SEL-POWER-E2E: not PID 1\n", stderr);
        return 1;
    }

    pid_t helper = fork();

    if (helper < 0) {
        perror("SEL-POWER-E2E: fork");
        return 1;
    }

    if (helper == 0) {
        int ready = 0;

        for (int i = 0; i < 200; ++i) {
            if (access("/run/selyra-power/control.sock",
                       F_OK) == 0) {
                ready = 1;
                break;
            }
            usleep(100000);
        }

        if (!ready) {
            fputs("SEL-POWER-E2E: socket missing\n", stderr);
            _exit(1);
        }

        sleep(3);

        printf("SEL-POWER-E2E: requesting %s\n",
               SELYRA_TEST_ACTION);
        fflush(stdout);

        if (send_request() != 0) {
            fputs("SEL-POWER-E2E: request failed\n", stderr);
            _exit(1);
        }

        puts("SEL-POWER-E2E: request acknowledged");
        fflush(stdout);

        sleep(30);
        fputs("SEL-POWER-E2E: transition timeout\n", stderr);
        _exit(1);
    }

    execl("/selyra-init", "init", (char *)NULL);
    perror("SEL-POWER-E2E: exec init");
    return 127;
}
