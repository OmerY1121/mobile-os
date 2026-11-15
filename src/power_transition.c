#include "power_transition.h"

#include <errno.h>
#include <sys/reboot.h>
#include <unistd.h>

int selyra_power_command(
    enum selyra_power_action action,
    int *command)
{
    if (!command) {
        errno = EINVAL;
        return -1;
    }

    switch (action) {
    case SELYRA_POWER_OFF:
        *command = RB_POWER_OFF;
        return 0;

    case SELYRA_POWER_REBOOT:
        *command = RB_AUTOBOOT;
        return 0;

    default:
        errno = EINVAL;
        return -1;
    }
}

int selyra_power_transition(
    enum selyra_power_action action)
{
    int command;

    if (selyra_power_command(
            action, &command) != 0)
        return -1;

    if (getpid() != 1) {
        errno = EPERM;
        return -1;
    }

    sync();

    return reboot(command);
}
