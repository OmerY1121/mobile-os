#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/service_config.h"

int main(void)
{
    char path[] = "/tmp/selyra-policy-XXXXXX";

    assert(mkdtemp(path));

    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    assert(dir >= 0);

    struct mo_service_config cfg = {1, 3, 1, 3};
    struct mo_service_config loaded = {0};

    assert(selyra_service_policy_load(dir, &loaded) == 1);

    cfg.logd_enabled = 0;

    assert(selyra_service_policy_save(dir, &cfg) == 0);
    assert(selyra_service_policy_load(dir, &loaded) == 0);
    assert(loaded.logd_enabled == 0);
    assert(loaded.statusd_enabled == 1);
    assert(loaded.logd_restart_limit == 3);
    assert(loaded.statusd_restart_limit == 3);

    puts("PASS: Persistent service policy");

    cfg.logd_enabled = 1;

    assert(selyra_service_policy_save(dir, &cfg) == 0);

    cfg.logd_enabled = 2;

    assert(selyra_service_policy_save(dir, &cfg) == -1);

    cfg.logd_enabled = 1;

    assert(selyra_service_policy_load(dir, &loaded) == 0);
    assert(loaded.logd_enabled == 1);

    puts("PASS: Invalid policy preserves saved values");

    assert(fchmodat(dir, "services.conf", 0644, 0) == 0);
    assert(selyra_service_policy_load(dir, &loaded) == -1);
    assert(selyra_service_policy_save(dir, &cfg) == -1);

    assert(unlinkat(dir, "services.conf", 0) == 0);
    assert(symlinkat("missing", dir, "services.conf") == 0);

    assert(selyra_service_policy_load(dir, &loaded) == -1);
    assert(selyra_service_policy_save(dir, &cfg) == -1);

    assert(unlinkat(dir, "services.conf", 0) == 0);
    assert(fchmod(dir, 0755) == 0);

    assert(selyra_service_policy_load(dir, &loaded) == -1);
    assert(selyra_service_policy_save(dir, &cfg) == -1);

    assert(fchmod(dir, 0700) == 0);
    assert(close(dir) == 0);
    assert(rmdir(path) == 0);

    puts("PASS: Unsafe policy paths rejected");
    puts("All Selyra service policy tests passed.");

    return 0;
}
