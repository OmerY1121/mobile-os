#ifndef MOBILE_OS_SERVICE_CONFIG_H
#define MOBILE_OS_SERVICE_CONFIG_H

struct mo_service_config {
    int statusd_enabled;
    int statusd_restart_limit;
    int logd_enabled;
    int logd_restart_limit;
};

int mo_service_config_load(
    const char *path,
    struct mo_service_config *result
);


/* load: 0 valid, 1 missing, -1 error */
/* save: 0 durable, -1 failed, -2 uncertain */
int selyra_service_policy_load(int dirfd,
                               struct mo_service_config *cfg);

int selyra_service_policy_save(int dirfd,
                               const struct mo_service_config *cfg);

#endif
