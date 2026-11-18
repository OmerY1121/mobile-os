#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/config_engine.h"

int main(void)
{
    struct selyra_config cfg;

    selyra_config_defaults(&cfg);

    assert(strcmp(selyra_config_get(&cfg, "system.hostname"), "selyra") == 0);
    assert(strcmp(selyra_config_get(&cfg, "logging.max_kib"), "64") == 0);
    assert(selyra_config_key(0));
    assert(selyra_config_key(SELYRA_CONFIG_COUNT) == NULL);

    puts("PASS: Configuration defaults");

    assert(selyra_config_set(&cfg, "system.hostname", "selyra-test") == 0);
    assert(selyra_config_set(&cfg, "logging.max_kib", "128") == 0);

    assert(strcmp(selyra_config_get(&cfg, "system.hostname"), "selyra-test") == 0);
    assert(strcmp(selyra_config_get(&cfg, "logging.max_kib"), "128") == 0);

    puts("PASS: Valid configuration updates");

    const char *bad_hosts[] = {
        "", "-test", "test-", "bad name",
        "bad\nname", "a/b", "UPPER"
    };

    for (unsigned i = 0; i < sizeof(bad_hosts) / sizeof(bad_hosts[0]); ++i)
        assert(selyra_config_set(&cfg, "system.hostname", bad_hosts[i]) < 0);

    char long_host[65];

    memset(long_host, 'a', 64);
    long_host[64] = '\0';

    assert(selyra_config_set(&cfg, "system.hostname", long_host) < 0);

    long_host[63] = '\0';

    assert(selyra_config_set(&cfg, "system.hostname", long_host) == 0);
    assert(selyra_config_set(&cfg, "system.hostname", "selyra-test") == 0);

    const char *bad_sizes[] = {
        "", "0", "15", "4097", "-1", "+64",
        "064", "64.5", "64KiB",
        "999999999999999999999999"
    };

    for (unsigned i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); ++i)
        assert(selyra_config_set(&cfg, "logging.max_kib", bad_sizes[i]) < 0);

    assert(selyra_config_set(&cfg, "unknown.key", "1") < 0);
    assert(selyra_config_set(&cfg, "logging.max_kib", NULL) < 0);
    assert(selyra_config_get(&cfg, "unknown.key") == NULL);

    assert(strcmp(selyra_config_get(&cfg, "system.hostname"), "selyra-test") == 0);
    assert(strcmp(selyra_config_get(&cfg, "logging.max_kib"), "128") == 0);

    puts("PASS: Invalid updates rejected without changing settings");

    assert(selyra_config_set(&cfg, "logging.max_kib", "16") == 0);
    assert(selyra_config_set(&cfg, "logging.max_kib", "4096") == 0);
    assert(selyra_config_reset(&cfg, "logging.max_kib") == 0);
    assert(selyra_config_reset(&cfg, "system.hostname") == 0);
    assert(selyra_config_reset(&cfg, "unknown.key") < 0);

    assert(strcmp(selyra_config_get(&cfg, "logging.max_kib"), "64") == 0);
    assert(strcmp(selyra_config_get(&cfg, "system.hostname"), "selyra") == 0);

    puts("PASS: Configuration reset and limits");
    puts("All Selyra configuration engine tests passed.");

    return 0;
}
