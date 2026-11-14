#define _GNU_SOURCE
#include "service_config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

int mo_service_config_load(
    const char *path,
    struct mo_service_config *result
)
{
    if (!path || !result)
        return -1;

    int fd = open(path, O_RDONLY |
                  O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0)
        return -1;

    struct stat st;

    if (fstat(fd, &st) < 0 ||
        !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() ||
        (st.st_mode & 022) != 0 ||
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
