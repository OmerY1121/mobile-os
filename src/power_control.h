#ifndef SELYRA_POWER_CONTROL_H
#define SELYRA_POWER_CONTROL_H

#include <sys/types.h>

enum selyra_power_action {
    SELYRA_POWER_NONE = 0,
    SELYRA_POWER_OFF,
    SELYRA_POWER_REBOOT
};

/* 0: accepted, -1: invalid, -2: permission denied */
int selyra_power_parse(const char *request, uid_t peer_uid,
                       enum selyra_power_action *action);

#endif
