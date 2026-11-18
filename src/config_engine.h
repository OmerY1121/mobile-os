#ifndef SELYRA_CONFIG_ENGINE_H
#define SELYRA_CONFIG_ENGINE_H

#define SELYRA_CONFIG_COUNT 2
#define SELYRA_CONFIG_VALUE_SIZE 64

struct selyra_config {
    char values[SELYRA_CONFIG_COUNT][SELYRA_CONFIG_VALUE_SIZE];
};

void selyra_config_defaults(struct selyra_config *cfg);
const char *selyra_config_key(unsigned index);
const char *selyra_config_get(const struct selyra_config *cfg,
                              const char *key);
int selyra_config_set(struct selyra_config *cfg,
                      const char *key, const char *value);
int selyra_config_reset(struct selyra_config *cfg,
                        const char *key);

#endif
