#ifndef MOBILE_OS_SERVICE_STATE_H
#define MOBILE_OS_SERVICE_STATE_H

#include <stddef.h>

struct mo_service_state {
    const char *name;
    const char *state;
    int failures;
};

int mo_state_write(
    const char *path,
    const struct mo_service_state *services,
    size_t count
);

int mo_state_read(
    const char *path,
    char *buffer,
    size_t capacity
);

#endif
