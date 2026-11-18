#define _GNU_SOURCE
#include "service_config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CONFIG_SIZE 4096

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        ++s;

    size_t len = strlen(s);

    while (len &&
           isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';

    return s;
}

static int parse_number(
    const char *text, int minimum, int maximum,
    int *result
)
{
    char *end;
    errno = 0;

    if (!*text)
        return -1;

    long value = strtol(text, &end, 10);

    if (errno || *end ||
        value < minimum || value > maximum)
        return -1;

    *result = (int)value;
    return 0;
}

static int load_at(
    int dirfd, const char *path,
    struct mo_service_config *result, int strict
)
{
    if (!path || !result)
        return -1;

    int fd = openat(dirfd, path, O_RDONLY |
                    O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0)
        return strict && errno == ENOENT ? 1 : -1;

    struct stat st;

    if (fstat(fd, &st) < 0 ||
        !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() ||
        (st.st_mode & 022) != 0 ||
        (strict && ((st.st_mode & 07777) != 0600 ||
                    st.st_nlink != 1)) ||
        st.st_size > MAX_CONFIG_SIZE) {
        close(fd);
        return -1;
    }

    char data[MAX_CONFIG_SIZE + 1];
    size_t used = 0;

    while (used <= MAX_CONFIG_SIZE) {
        ssize_t n = read(
            fd, data + used,
            MAX_CONFIG_SIZE + 1 - used
        );

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0) {
            close(fd);
            return -1;
        }

        if (n == 0)
            break;

        used += (size_t)n;
    }

    close(fd);

    if (used > MAX_CONFIG_SIZE ||
        memchr(data, '\0', used))
        return -1;

    data[used] = '\0';

    struct mo_service_config cfg = {0};
    unsigned seen = 0;
    char *save = NULL;

    for (char *line = strtok_r(data, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {

        if (strlen(line) > 255)
            return -1;

        char *key = trim(line);

        if (!*key || *key == '#')
            continue;

        char *equals = strchr(key, '=');

        if (!equals)
            return -1;

        *equals++ = '\0';

        key = trim(key);
        char *value = trim(equals);

        const char *names[] = {
            "statusd.enabled",
            "statusd.restart_limit",
            "logd.enabled",
            "logd.restart_limit"
        };

        int *fields[] = {
            &cfg.statusd_enabled,
            &cfg.statusd_restart_limit,
            &cfg.logd_enabled,
            &cfg.logd_restart_limit
        };

        int index = -1;

        for (int i = 0; i < 4; ++i) {
            if (strcmp(key, names[i]) == 0) {
                index = i;
                break;
            }
        }

        if (index < 0 ||
            (seen & (1u << index)))
            return -1;

        int minimum = index % 2 == 0 ? 0 : 1;
        int maximum = index % 2 == 0 ? 1 : 10;

        if (parse_number(value, minimum, maximum,
                         fields[index]) < 0)
            return -1;

        seen |= 1u << index;
    }

    if (seen != 15u)
        return -1;

    *result = cfg;
    return 0;
}

static int private_dir(int dirfd)
{
    struct stat st;

    return fstat(dirfd, &st) == 0 &&
        S_ISDIR(st.st_mode) &&
        st.st_uid == geteuid() &&
        (st.st_mode & 07777) == 0700;
}

int mo_service_config_load(const char *path,
                           struct mo_service_config *result)
{
    return load_at(AT_FDCWD, path, result, 0);
}

int selyra_service_policy_load(int dirfd,
                               struct mo_service_config *cfg)
{
    if (!private_dir(dirfd))
        return -1;

    return load_at(dirfd, "services.conf", cfg, 1);
}

int selyra_service_policy_save(int dirfd,
                               const struct mo_service_config *cfg)
{
    if (!private_dir(dirfd) || !cfg ||
        cfg->statusd_enabled < 0 || cfg->statusd_enabled > 1 ||
        cfg->logd_enabled < 0 || cfg->logd_enabled > 1 ||
        cfg->statusd_restart_limit < 1 || cfg->statusd_restart_limit > 10 ||
        cfg->logd_restart_limit < 1 || cfg->logd_restart_limit > 10)
        return -1;

    struct stat st;

    if (fstatat(dirfd, "services.conf", &st,
                AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(st.st_mode) ||
            st.st_uid != geteuid() ||
            st.st_nlink != 1 ||
            (st.st_mode & 07777) != 0600)
            return -1;
    } else if (errno != ENOENT) {
        return -1;
    }

    char temp[64];
    int fd = -1;

    for (unsigned i = 0; i < 100; ++i) {
        snprintf(temp, sizeof(temp),
                 ".services-%ld-%u.tmp", (long)getpid(), i);

        fd = openat(dirfd, temp, O_WRONLY | O_CREAT | O_EXCL |
                    O_NOFOLLOW | O_CLOEXEC, 0600);

        if (fd >= 0 || errno != EEXIST)
            break;
    }

    if (fd < 0)
        return -1;

    int ok = fchmod(fd, 0600) == 0 &&
        dprintf(fd, "statusd.enabled=%d\nstatusd.restart_limit=%d\n"
                    "logd.enabled=%d\nlogd.restart_limit=%d\n",
                cfg->statusd_enabled, cfg->statusd_restart_limit,
                cfg->logd_enabled, cfg->logd_restart_limit) > 0 &&
        fsync(fd) == 0;

    if (close(fd) < 0)
        ok = 0;

    if (!ok || renameat(dirfd, temp, dirfd,
                        "services.conf") < 0) {
        unlinkat(dirfd, temp, 0);
        return -1;
    }

    return fsync(dirfd) == 0 ? 0 : -2;
}
