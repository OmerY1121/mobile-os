#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_powerctl(const char *command)
{
    pid_t child = fork();
    if (child < 0) return -1;

    if (child == 0) {
        execl("/bin/powerctl", "powerctl",
              command, (char *)NULL);
        perror("SEL-E2E: exec powerctl");
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
        fputs("SEL-E2E: test init is not PID 1\n", stderr);
        return 1;
    }

    pid_t helper = fork();
    if (helper < 0) {
        perror("SEL-E2E: fork");
        return 1;
    }

    if (helper == 0) {
        int ready = 0;

        for (int attempt = 0; attempt < 200; ++attempt) {
            if (access("/run/selyra-power/control.sock",
                       F_OK) == 0) {
                ready = 1;
                break;
            }
            usleep(100000);
        }

        if (!ready) {
            fputs("SEL-E2E: power socket missing\n", stderr);
            _exit(1);
        }

        sleep(3);
        puts("SEL-E2E: sending reboot request");
        fflush(stdout);

        if (run_powerctl("reboot") != 0) {
            puts("SEL-E2E: reboot request not acknowledged");
            fflush(stdout);
            _exit(1);
        }

        puts("SEL-E2E: reboot request acknowledged");
        fflush(stdout);

        sleep(2);

        if (run_powerctl("poweroff") != 2) {
            puts("SEL-E2E: duplicate power request not rejected");
            fflush(stdout);
            _exit(1);
        }

        puts("SEL-E2E: duplicate power request rejected");
        fflush(stdout);
        _exit(0);
    }

    execl("/selyra-init", "init", (char *)NULL);
    perror("SEL-E2E: exec init");
    return 127;
}
