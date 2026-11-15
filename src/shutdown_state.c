#include "shutdown_state.h"

int selyra_shutdown_begin(
    struct selyra_shutdown_state *state,
    enum selyra_power_action action,
    time_t now)
{
    if (!state ||
        state->phase != SELYRA_SHUTDOWN_RUNNING ||
        (action != SELYRA_POWER_OFF &&
         action != SELYRA_POWER_REBOOT) ||
        now < 0)
        return -1;

    state->action = action;
    state->started_at = now;
    state->phase = SELYRA_SHUTDOWN_GRACEFUL;

    return 0;
}

int selyra_shutdown_restart_allowed(
    const struct selyra_shutdown_state *state)
{
    return state &&
        state->phase == SELYRA_SHUTDOWN_RUNNING;
}

int selyra_shutdown_check_timeout(
    struct selyra_shutdown_state *state,
    time_t now,
    unsigned int grace_seconds)
{
    if (!state ||
        state->phase != SELYRA_SHUTDOWN_GRACEFUL ||
        grace_seconds == 0 ||
        now < state->started_at)
        return -1;

    if ((unsigned long long)
        (now - state->started_at) < grace_seconds)
        return 0;

    state->phase = SELYRA_SHUTDOWN_FORCE;
    return 1;
}

int selyra_shutdown_finish(
    struct selyra_shutdown_state *state)
{
    if (!state ||
        (state->phase != SELYRA_SHUTDOWN_GRACEFUL &&
         state->phase != SELYRA_SHUTDOWN_FORCE))
        return -1;

    state->phase = SELYRA_SHUTDOWN_READY;
    return 0;
}
