#ifndef SELYRA_CONFIG_STORE_H
#define SELYRA_CONFIG_STORE_H
#include "config_engine.h"
#include <sys/types.h>

enum config_store_result {
    CONFIG_STORE_OK = 0,
    CONFIG_STORE_MISSING = 1,
    CONFIG_STORE_INVALID = 2,
    CONFIG_STORE_ERROR = -1,
    CONFIG_STORE_UNCERTAIN = -2
};

/* dirfd must refer to a private directory owned by the caller. */
int selyra_config_load(int dirfd, struct selyra_config *cfg);
int selyra_config_save(int dirfd, const struct selyra_config *cfg);

int selyra_config_load_owned(int dirfd,
                             struct selyra_config *cfg,
                             uid_t owner);

#endif
