#ifndef SELYRA_SHUTDOWN_STATE_H
#define SELYRA_SHUTDOWN_STATE_H

#include <time.h>
#include "power_control.h"

enum selyra_shutdown_phase {
    SELYRA_SHUTDOWN_RUNNING = 0,
    SELYRA_SHUTDOWN_GRACEFUL,
    SELYRA_SHUTDOWN_FORCE,
    SELYRA_SHUTDOWN_READY
};

struct selyra_shutdown_state {
    enum selyra_shutdown_phase phase;
    enum selyra_power_action action;
    time_t started_at;
};

int selyra_shutdown_begin(
    struct selyra_shutdown_state *state,
    enum selyra_power_action action,
    time_t now
);

int selyra_shutdown_restart_allowed(
    const struct selyra_shutdown_state *state
);

int selyra_shutdown_check_timeout(
    struct selyra_shutdown_state *state,
    time_t now,
    unsigned int grace_seconds
);

int selyra_shutdown_finish(
    struct selyra_shutdown_state *state
);

#endif
