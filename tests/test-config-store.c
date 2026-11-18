#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/config_store.h"

static void replace_file(int dir, const char *data, size_t len)
{
    int fd = openat(dir, "settings.conf", O_WRONLY | O_TRUNC |
                    O_CREAT | O_NOFOLLOW, 0600);

    assert(fd >= 0);
    assert(write(fd, data, len) == (ssize_t)len);
    assert(close(fd) == 0);
}

int main(void)
{
    char path[] = "/tmp/selyra-config-XXXXXX";

    assert(mkdtemp(path));

    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(dir >= 0);

    struct selyra_config cfg, loaded;

    selyra_config_defaults(&cfg);
    selyra_config_defaults(&loaded);

    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_MISSING);

    puts("PASS: Missing configuration detected");

    assert(selyra_config_set(&cfg, "system.hostname", "selyra-test") == 0);
    assert(selyra_config_set(&cfg, "logging.max_kib", "256") == 0);

    assert(selyra_config_save(dir, &cfg) == CONFIG_STORE_OK);
    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_OK);

    assert(strcmp(selyra_config_get(&loaded, "system.hostname"), "selyra-test") == 0);
    assert(strcmp(selyra_config_get(&loaded, "logging.max_kib"), "256") == 0);

    struct stat old, after;

    assert(fstatat(dir, "settings.conf", &old, 0) == 0);
    assert((old.st_mode & 07777) == 0600);

    assert(selyra_config_set(&cfg, "logging.max_kib", "512") == 0);
    assert(selyra_config_save(dir, &cfg) == CONFIG_STORE_OK);

    assert(fstatat(dir, "settings.conf", &after, 0) == 0);
    assert(old.st_ino != after.st_ino);

    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_OK);
    assert(strcmp(selyra_config_get(&loaded, "logging.max_kib"), "512") == 0);

    puts("PASS: Atomic configuration save and reload");

    const char *bad[] = {
        "system.hostname=test\n",
        "system.hostname=bad name\nlogging.max_kib=64\n",
        "system.hostname=test\nlogging.max_kib=0\n",
        "system.hostname=test\nsystem.hostname=other\nlogging.max_kib=64\n",
        "system.hostname=test\nlogging.max_kib=64\nunknown=1\n",
        "system.hostname=test\nlogging.max_kib=64"
    };

    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        replace_file(dir, bad[i], strlen(bad[i]));

        assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_INVALID);
        assert(strcmp(selyra_config_get(&loaded, "logging.max_kib"), "512") == 0);
        assert(strcmp(selyra_config_get(&loaded, "system.hostname"), "selyra-test") == 0);
    }

    char oversized[513];
    memset(oversized, 'a', sizeof(oversized));

    replace_file(dir, oversized, sizeof(oversized));
    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_INVALID);

    const char binary[] =
        "system.hostname=test\0\nlogging.max_kib=64\n";

    replace_file(dir, binary, sizeof(binary)-1);
    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_INVALID);

    puts("PASS: Corrupt configuration rejected");

    assert(fchmodat(dir, "settings.conf", 0644, 0) == 0);
    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_ERROR);
    assert(selyra_config_save(dir, &cfg) == CONFIG_STORE_ERROR);

    assert(unlinkat(dir, "settings.conf", 0) == 0);
    assert(symlinkat("missing-target", dir, "settings.conf") == 0);

    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_ERROR);
    assert(selyra_config_save(dir, &cfg) == CONFIG_STORE_ERROR);

    assert(unlinkat(dir, "settings.conf", 0) == 0);
    assert(fchmod(dir, 0755) == 0);

    assert(selyra_config_save(dir, &cfg) == CONFIG_STORE_ERROR);
    assert(selyra_config_load(dir, &loaded) == CONFIG_STORE_ERROR);

    assert(fchmod(dir, 0700) == 0);

    puts("PASS: Unsafe files and directories rejected");

    assert(close(dir) == 0);
    assert(rmdir(path) == 0);

    puts("All Selyra configuration store tests passed.");

    return 0;
}
