#ifndef SELYRA_POWER_TRANSITION_H
#define SELYRA_POWER_TRANSITION_H

#include "power_control.h"

int selyra_power_command(
    enum selyra_power_action action,
    int *command
);

int selyra_power_transition(
    enum selyra_power_action action
);

#endif
