#ifndef MOBILE_OS_SERVICE_QUERY_H
#define MOBILE_OS_SERVICE_QUERY_H

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mo_query_services(
    const char *path, char *output, size_t capacity)
{
    if (!path || !output || capacity < 16)
        return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if (fd < 0)
        return -1;

    struct stat st;

    if (fstat(fd, &st) < 0 ||
        !S_ISREG(st.st_mode) ||
        (st.st_uid != 0 && st.st_uid != geteuid()) ||
        (st.st_mode & 022) ||
        st.st_size <= 0 || st.st_size > 512) {
        close(fd);
        return -1;
    }

    char input[512];
    size_t length = (size_t)st.st_size;
    size_t used = 0;

    while (used < length) {
        size_t available = sizeof(input) - used;
        size_t wanted = length - used;

        if (wanted < available)
            available = wanted;

        ssize_t n = read(fd, input + used, available);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            close(fd);
            return -1;
        }

        used += (size_t)n;
    }

    close(fd);

    if (input[length - 1] != '\n')
        return -1;

    const char *prefix = "services: ";
    size_t pos = strlen(prefix);

    if (pos >= capacity)
        return -1;

    memcpy(output, prefix, pos);

    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)input[i];

        if (pos + 3 >= capacity)
            return -1;

        if (c == '\n') {
            if (i == length - 1) {
                output[pos++] = '\n';
            } else {
                output[pos++] = ';';
                output[pos++] = ' ';
            }
        } else if (
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '=' || c == ' ' ||
            c == '-' || c == '_'
        ) {
            output[pos++] = (char)c;
        } else {
            return -1;
        }
    }

    output[pos] = '\0';
    return 0;
}

#endif
