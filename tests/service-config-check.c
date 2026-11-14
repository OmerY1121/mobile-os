#include "../src/service_config.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;

    struct mo_service_config cfg;

    if (mo_service_config_load(argv[1], &cfg) < 0)
        return 1;

    printf("statusd=%d, retries=%d\n",
           cfg.statusd_enabled,
           cfg.statusd_restart_limit);

    printf("logd=%d, retries=%d\n",
           cfg.logd_enabled,
           cfg.logd_restart_limit);

    return 0;
}
