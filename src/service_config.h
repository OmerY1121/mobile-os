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

#endif
