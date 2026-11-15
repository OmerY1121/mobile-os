#ifndef SELYRA_ENV_H
#define SELYRA_ENV_H

#include <stdlib.h>

static inline const char *selyra_env_value(
    const char *name,
    const char *legacy_name)
{
    const char *value = getenv(name);

    if (value && *value)
        return value;

    value = getenv(legacy_name);

    if (value && *value)
        return value;

    return NULL;
}

#endif
