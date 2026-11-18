#define _GNU_SOURCE
#include "service_control.h"
#include <poll.h>
#include <string.h>
#include <sys/socket.h>

int selyra_service_parse(const char *data, size_t length,
                         uid_t uid,
                         struct selyra_service_request *request)
{
    if (!request)
        return -1;

    *request = (struct selyra_service_request){0};

    if (uid != 0)
        return -2;

    if (!data || length < 2 || length > 32 ||
        data[length - 1] != '\n' ||
        memchr(data, '\0', length))
        return -1;

    static const struct {
        const char *prefix;
        enum selyra_service_action action;
    } verbs[] = {
        {"START ", SERVICE_START},
        {"STOP ", SERVICE_STOP},
        {"RESTART ", SERVICE_RESTART},
        {"ENABLE ", SERVICE_ENABLE},
        {"DISABLE ", SERVICE_DISABLE}
    };

    for (size_t i = 0;
         i < sizeof(verbs)/sizeof(verbs[0]); ++i) {

        size_t n = strlen(verbs[i].prefix);

        if (length <= n ||
            memcmp(data, verbs[i].prefix, n))
            continue;

        enum selyra_service_target target;
        size_t left = length - n;

        if (left == 5 &&
            !memcmp(data + n, "logd\n", 5))
            target = SERVICE_LOGD;

        else if (left == 8 &&
                 !memcmp(data + n, "statusd\n", 8))
            target = SERVICE_STATUSD;

        else
            return -1;

        request->action = verbs[i].action;
        request->target = target;

        return 0;
    }

    return -1;
}

int selyra_service_receive(int fd,
                           struct selyra_service_request *request)
{
    if (!request)
        return -1;

    *request = (struct selyra_service_request){0};

    struct ucred peer;
    socklen_t size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &size) < 0 ||
        size != sizeof(peer) || peer.uid != 0)
        return -2;

    int type;
    size = sizeof(type);

    if (getsockopt(fd, SOL_SOCKET, SO_TYPE,
                   &type, &size) < 0 ||
        type != SOCK_SEQPACKET)
        return -1;

    struct pollfd p = {
        .fd = fd,
        .events = POLLIN
    };

    if (poll(&p, 1, 200) <= 0 ||
        !(p.revents & POLLIN))
        return -1;

    char data[32];

    ssize_t n = recv(fd, data, sizeof(data),
                     MSG_DONTWAIT | MSG_TRUNC);

    if (n <= 0 || (size_t)n > sizeof(data))
        return -1;

    return selyra_service_parse(
        data, (size_t)n, peer.uid, request);
}
