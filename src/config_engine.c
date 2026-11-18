#include "config_engine.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int valid_hostname(const char *value)
{
    size_t n = strlen(value);

    if (!n || n > 63 ||
        value[0] == '-' || value[n - 1] == '-')
        return 0;

    for (size_t i = 0; i < n; ++i) {
        char c = value[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-'))
            return 0;
    }

    return 1;
}

static int valid_log_size(const char *value)
{
    if (*value < '1' || *value > '9')
        return 0;

    for (const char *p = value; *p; ++p)
        if (*p < '0' || *p > '9')
            return 0;

    errno = 0;
    unsigned long n = strtoul(value, NULL, 10);

    return !errno && n >= 16 && n <= 4096;
}

static const struct {
    const char *key;
    const char *initial;
    int (*validate)(const char *);
} schema[SELYRA_CONFIG_COUNT] = {
    {"system.hostname", "selyra", valid_hostname},
    {"logging.max_kib", "64", valid_log_size}
};

static int find_key(const char *key)
{
    if (key)
        for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i)
            if (strcmp(key, schema[i].key) == 0)
                return (int)i;

    return -1;
}

void selyra_config_defaults(struct selyra_config *cfg)
{
    if (!cfg)
        return;

    memset(cfg, 0, sizeof(*cfg));

    for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i)
        strcpy(cfg->values[i], schema[i].initial);
}

const char *selyra_config_key(unsigned index)
{
    return index < SELYRA_CONFIG_COUNT
        ? schema[index].key : NULL;
}

const char *selyra_config_get(const struct selyra_config *cfg,
                              const char *key)
{
    int i = find_key(key);

    return cfg && i >= 0 ? cfg->values[i] : NULL;
}

int selyra_config_set(struct selyra_config *cfg,
                      const char *key, const char *value)
{
    int i = find_key(key);

    if (!cfg || i < 0 || !value ||
        strlen(value) >= SELYRA_CONFIG_VALUE_SIZE ||
        !schema[i].validate(value))
        return -1;

    memmove(cfg->values[i], value, strlen(value) + 1);

    return 0;
}

int selyra_config_reset(struct selyra_config *cfg,
                        const char *key)
{
    int i = find_key(key);

    return i < 0 ? -1 :
        selyra_config_set(cfg, key, schema[i].initial);
}
