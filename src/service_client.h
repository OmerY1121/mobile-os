#ifndef SELYRA_SERVICE_CLIENT_H
#define SELYRA_SERVICE_CLIENT_H

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int service_command(const char *action,
                           const char *name)
{
    if (geteuid() != 0) {
        fputs("servicectl: root required\n", stderr);
        return 1;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = "/run/selyra-power/service.sock"
    };

    int fd = socket(AF_UNIX,
        SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (fd < 0) {
        perror("servicectl: socket");
        return 1;
    }

    int result = 1;

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("servicectl: connect");
        goto done;
    }

    struct ucred peer;
    socklen_t size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &size) < 0 ||
        size != sizeof(peer) ||
        peer.uid != 0 || peer.pid != 1) {

        fputs("servicectl: untrusted server\n", stderr);
        goto done;
    }

    char request[32], response[128];

    int n = snprintf(request, sizeof(request),
                     "%s %s\n", action, name);

    if (n < 0 || (size_t)n >= sizeof(request))
        goto done;

    if (send(fd, request, (size_t)n,
             MSG_NOSIGNAL) != n) {
        perror("servicectl: send");
        goto done;
    }

    struct pollfd p = {
        .fd = fd,
        .events = POLLIN
    };

    if (poll(&p, 1, 5000) <= 0) {
        fputs("servicectl: response timeout\n", stderr);
        goto done;
    }

    ssize_t received = recv(fd, response,
                           sizeof(response) - 1, MSG_TRUNC);

    if (received <= 0 ||
        (size_t)received >= sizeof(response) ||
        memchr(response, '\0', (size_t)received))
        goto done;

    response[received] = '\0';

    if (!strcmp(response, "OK accepted\n")) {
        puts("OK accepted");
        result = 0;
    } else {
        fputs("servicectl: request rejected\n", stderr);
    }

done:
    close(fd);
    return result;
}

#endif
