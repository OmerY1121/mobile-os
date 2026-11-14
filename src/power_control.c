#include "power_control.h"
#include <string.h>

int selyra_power_parse(const char *request, uid_t peer_uid,
                       enum selyra_power_action *action)
{
    if (!request || !action)
        return -1;

    *action = SELYRA_POWER_NONE;

    if (peer_uid != 0)
        return -2;

    if (strcmp(request, "POWEROFF\n") == 0) {
        *action = SELYRA_POWER_OFF;
        return 0;
    }

    if (strcmp(request, "REBOOT\n") == 0) {
        *action = SELYRA_POWER_REBOOT;
        return 0;
    }

    return -1;
}
