#include <errno.h>
#include <stdio.h>
#include <sys/reboot.h>
#include <unistd.h>

#include "../src/power_transition.h"

int main(void)
{
    int command = -1;

    if (selyra_power_command(
            SELYRA_POWER_OFF, &command) != 0 ||
        command != RB_POWER_OFF ||
        selyra_power_command(
            SELYRA_POWER_REBOOT, &command) != 0 ||
        command != RB_AUTOBOOT ||
        selyra_power_command(
            SELYRA_POWER_NONE, &command) != -1 ||
        selyra_power_command(
            SELYRA_POWER_OFF, NULL) != -1) {

        fputs("FAIL: power action mapping\n", stderr);
        return 1;
    }

    /* Never perform a real transition in this unit test. */
    if (getpid() == 1) {
        fputs("FAIL: test must not run as PID 1\n", stderr);
        return 1;
    }

    errno = 0;

    if (selyra_power_transition(
            SELYRA_POWER_OFF) != -1 ||
        errno != EPERM) {

        fputs("FAIL: unauthorized poweroff\n", stderr);
        return 1;
    }

    errno = 0;

    if (selyra_power_transition(
            SELYRA_POWER_REBOOT) != -1 ||
        errno != EPERM) {

        fputs("FAIL: unauthorized reboot\n", stderr);
        return 1;
    }

    puts("PASS: poweroff/reboot kernel command mapping");
    puts("PASS: non-PID-1 transition rejected");
    puts("PASS: no host power transition performed");

    return 0;
}
