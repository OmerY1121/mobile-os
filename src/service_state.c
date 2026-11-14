#define _GNU_SOURCE
#include "service_state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_SERVICES 16

static int valid_token(const char *s)
{
    if (!s || !*s)
        return 0;

    size_t len = strlen(s);

    if (len > 32)
        return 0;

    for (size_t i = 0; i < len; ++i) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return 0;
    }

    return 1;
}

int mo_state_write(
    const char *path,
    const struct mo_service_state *services,
    size_t count
)
{
    if (!path || !services ||
        count == 0 || count > MAX_SERVICES)
        return -1;

    char temporary[512];

    int n = snprintf(
        temporary, sizeof(temporary),
        "%s.tmp.XXXXXX", path
    );

    if (n < 0 || (size_t)n >= sizeof(temporary))
        return -1;

    int fd = mkstemp(temporary);

    if (fd < 0)
        return -1;

    FILE *file = fdopen(fd, "w");

    if (!file) {
        close(fd);
        unlink(temporary);
        return -1;
    }

    int failed = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!valid_token(services[i].name) ||
            !valid_token(services[i].state) ||
            services[i].failures < 0 ||
            services[i].failures > 1000) {
            failed = 1;
            break;
        }

        if (fprintf(
            file, "%s=%s failures=%d\n",
            services[i].name,
            services[i].state,
            services[i].failures
        ) < 0) {
            failed = 1;
            break;
        }
    }

    if (fflush(file) != 0)
        failed = 1;

    if (!failed && fchmod(fd, 0644) != 0)
        failed = 1;

    if (!failed && fsync(fd) != 0)
        failed = 1;

    if (fclose(file) != 0)
        failed = 1;

    if (!failed && rename(temporary, path) != 0)
        failed = 1;

    if (failed)
        unlink(temporary);

    return failed ? -1 : 0;
}

int mo_state_read(
    const char *path,
    char *buffer,
    size_t capacity
)
{
    if (!path || !buffer || capacity < 2)
        return -1;

    int fd = open(
        path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW
    );

    if (fd < 0)
        return -1;

    struct stat st;

    if (fstat(fd, &st) != 0 ||
        !S_ISREG(st.st_mode) ||
        st.st_size < 0 ||
        (size_t)st.st_size >= capacity) {
        close(fd);
        return -1;
    }

    size_t used = 0;

    while (used < capacity - 1) {
        ssize_t n = read(
            fd, buffer + used,
            capacity - used - 1
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
    buffer[used] = '\0';

    if (used == 0 || buffer[used - 1] != '\n' ||
        memchr(buffer, '\0', used))
        return -1;

    return 0;
}
