#ifndef MOBILE_OS_LOG_CLIENT_H
#define MOBILE_OS_LOG_CLIENT_H

#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include "selyra_env.h"


static int mo_log_connect_timeout(
    int fd, const struct sockaddr_un *addr)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return -1;

    int rc = connect(
        fd, (const struct sockaddr *)addr,
        sizeof(*addr)
    );

    if (rc < 0) {
        if (errno != EINPROGRESS && errno != EINTR)
            return -1;

        struct timespec start, now;

        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
            return -1;

        struct pollfd pfd = {
            .fd = fd,
            .events = POLLOUT
        };

        for (;;) {
            if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
                return -1;

            long elapsed =
                (now.tv_sec - start.tv_sec) * 1000L +
                (now.tv_nsec - start.tv_nsec) / 1000000L;

            if (elapsed >= 1000) {
                errno = ETIMEDOUT;
                return -1;
            }

            rc = poll(&pfd, 1, (int)(1000 - elapsed));

            if (rc > 0)
                break;

            if (rc == 0) {
                errno = ETIMEDOUT;
                return -1;
            }

            if (errno != EINTR)
                return -1;
        }

        int error = 0;
        socklen_t size = sizeof(error);

        if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                       &error, &size) < 0)
            return -1;

        if (error != 0) {
            errno = error;
            return -1;
        }
    }

    return fcntl(fd, F_SETFL, flags);
}

static int mo_log_event(const char *message)
{
    if (!message)
        return -1;

    size_t len = strlen(message);

    if (!len || len > 240)
        return -1;

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)message[i];
        if (c < 32 || c == 127)
            return -1;
    }

    const char *path = selyra_env_value("SELYRA_LOG_SOCKET", "MOBILE_OS_LOG_SOCKET");

    if (!path || !*path)
        path = "/run/selyra-logs/log.sock";

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof(addr.sun_path))
        return -1;

    strcpy(addr.sun_path, path);

    int fd = socket(AF_UNIX,
                    SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0)
        return -1;

    struct timeval timeout = {.tv_sec = 1};

    (void)setsockopt(fd, SOL_SOCKET,
                     SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET,
                     SO_SNDTIMEO, &timeout, sizeof(timeout));

    int result = -1;

    if (mo_log_connect_timeout(fd, &addr) < 0)
        goto done;

    char request[256];

    int count = snprintf(request, sizeof(request),
                         "WRITE %s\n", message);

    if (count < 0 || (size_t)count >= sizeof(request))
        goto done;

    size_t sent = 0;

    while (sent < (size_t)count) {
        ssize_t n = send(fd, request + sent,
                         (size_t)count - sent, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0)
            goto done;

        sent += (size_t)n;
    }

    (void)shutdown(fd, SHUT_WR);

    char response[16];
    ssize_t n = recv(fd, response, sizeof(response), 0);

    if (n == 3 && memcmp(response, "OK\n", 3) == 0)
        result = 0;

done:
    close(fd);
    return result;
}

#endif
