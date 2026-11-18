#define _GNU_SOURCE
#include "config_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_FILE "settings.conf"
#define CONFIG_LIMIT 512

static int private_directory(int fd, uid_t owner)
{
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
        st.st_uid == owner && (st.st_mode & 07777) == 0700;
}

static int private_file(const struct stat *st, uid_t owner)
{
    return S_ISREG(st->st_mode) && st->st_uid == owner &&
        st->st_nlink == 1 && (st->st_mode & 07777) == 0600;
}

int selyra_config_load_owned(int dirfd,
                             struct selyra_config *cfg,
                             uid_t owner)
{
    if (!cfg || !private_directory(dirfd, owner))
        return CONFIG_STORE_ERROR;

    int fd = openat(dirfd, CONFIG_FILE,
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? CONFIG_STORE_MISSING : CONFIG_STORE_ERROR;

    struct stat st;
    if (fstat(fd, &st) < 0 || !private_file(&st, owner)) {
        close(fd);
        return CONFIG_STORE_ERROR;
    }

    char buf[CONFIG_LIMIT + 1];
    size_t used = 0;

    while (used < sizeof(buf)) {
        ssize_t n = read(fd, buf + used, sizeof(buf) - used);

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0) {
            close(fd);
            return CONFIG_STORE_ERROR;
        }

        if (!n)
            break;

        used += (size_t)n;
    }

    if (close(fd) < 0)
        return CONFIG_STORE_ERROR;

    if (!used || used >= sizeof(buf) ||
        memchr(buf, '\0', used) || buf[used - 1] != '\n')
        return CONFIG_STORE_INVALID;

    buf[used] = '\0';

    struct selyra_config next;
    selyra_config_defaults(&next);

    int seen[SELYRA_CONFIG_COUNT] = {0};

    for (char *line = buf; *line;) {
        char *end = strchr(line, '\n');
        *end = '\0';

        char *value = strchr(line, '=');

        if (!value)
            return CONFIG_STORE_INVALID;

        *value++ = '\0';

        unsigned i;

        for (i = 0; i < SELYRA_CONFIG_COUNT; ++i)
            if (!strcmp(line, selyra_config_key(i)))
                break;

        if (i == SELYRA_CONFIG_COUNT || seen[i] ||
            selyra_config_set(&next, line, value) < 0)
            return CONFIG_STORE_INVALID;

        seen[i] = 1;
        line = end + 1;
    }

    for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i)
        if (!seen[i])
            return CONFIG_STORE_INVALID;

    *cfg = next;

    return CONFIG_STORE_OK;
}

int selyra_config_load(int dirfd, struct selyra_config *cfg)
{
    return selyra_config_load_owned(dirfd, cfg, geteuid());
}

int selyra_config_save(int dirfd, const struct selyra_config *cfg)
{
    if (!cfg || !private_directory(dirfd, geteuid()))
        return CONFIG_STORE_ERROR;

    char buf[CONFIG_LIMIT + 1];
    size_t used = 0;

    struct selyra_config checked;
    selyra_config_defaults(&checked);

    for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i) {
        const char *key = selyra_config_key(i);
        const char *value = selyra_config_get(cfg, key);

        if (!value || !memchr(value, '\0', SELYRA_CONFIG_VALUE_SIZE) ||
            selyra_config_set(&checked, key, value) < 0)
            return CONFIG_STORE_ERROR;

        int n = snprintf(buf + used, sizeof(buf) - used,
                         "%s=%s\n", key, value);

        if (n < 0 || (size_t)n >= sizeof(buf) - used)
            return CONFIG_STORE_ERROR;

        used += (size_t)n;
    }

    struct stat st;

    if (fstatat(dirfd, CONFIG_FILE, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!private_file(&st, geteuid()))
            return CONFIG_STORE_ERROR;
    } else if (errno != ENOENT) {
        return CONFIG_STORE_ERROR;
    }

    char temp[64];
    int fd = -1;

    for (unsigned i = 0; i < 100; ++i) {
        snprintf(temp, sizeof(temp),
                 ".settings-%ld-%u.tmp", (long)getpid(), i);

        fd = openat(dirfd, temp, O_WRONLY | O_CREAT | O_EXCL |
                    O_NOFOLLOW | O_CLOEXEC, 0600);

        if (fd >= 0 || errno != EEXIST)
            break;
    }

    if (fd < 0)
        return CONFIG_STORE_ERROR;

    int ok = fchmod(fd, 0600) == 0;
    size_t offset = 0;

    while (ok && offset < used) {
        ssize_t n = write(fd, buf + offset, used - offset);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            ok = 0;
            break;
        }

        offset += (size_t)n;
    }

    if (ok && fsync(fd) < 0)
        ok = 0;

    if (close(fd) < 0)
        ok = 0;

    if (!ok || renameat(dirfd, temp, dirfd, CONFIG_FILE) < 0) {
        unlinkat(dirfd, temp, 0);
        return CONFIG_STORE_ERROR;
    }

    return fsync(dirfd) == 0
        ? CONFIG_STORE_OK : CONFIG_STORE_UNCERTAIN;
}
