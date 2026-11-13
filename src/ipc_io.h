#ifndef MOBILE_OS_IPC_IO_H
#define MOBILE_OS_IPC_IO_H

#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

static int ipc_set_timeout(int fd)
{
    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0)
        return -1;

    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                      &timeout, sizeof(timeout));
}

static int ipc_send_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t sent = send(fd, data, length, MSG_NOSIGNAL);

        if (sent < 0 && errno == EINTR)
            continue;

        if (sent <= 0)
            return -1;

        data += sent;
        length -= (size_t)sent;
    }

    return 0;
}

static ssize_t ipc_read_line(int fd, char *buffer, size_t capacity)
{
    if (capacity < 2) {
        errno = EINVAL;
        return -1;
    }

    size_t size = 0;

    while (size + 1 < capacity) {
        char ch;
        ssize_t received = recv(fd, &ch, 1, 0);

        if (received < 0 && errno == EINTR)
            continue;

        if (received < 0)
            return -1;

        if (received == 0) {
            errno = ECONNRESET;
            return -1;
        }

        buffer[size++] = ch;

        if (ch == '\n') {
            buffer[size] = '\0';
            return (ssize_t)size;
        }
    }

    errno = EMSGSIZE;
    return -1;
}

#endif
