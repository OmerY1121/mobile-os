#define _GNU_SOURCE
#include "power_socket.h"
#include "ipc_io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int selyra_power_socket_open(const char *path)
{
    if (!path || !*path) {
        errno = EINVAL;
        return -1;
    }

    struct sockaddr_un addr = {0};

    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    int fd = socket(AF_UNIX,
                    SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    0);

    if (fd < 0)
        return -1;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    struct stat st;

    if (lstat(path, &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            errno = EEXIST;
            goto fail;
        }

        if (unlink(path) < 0)
            goto fail;
    } else if (errno != ENOENT) {
        goto fail;
    }

    if (bind(fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0)
        goto fail;

    if (chmod(path, 0600) < 0 ||
        listen(fd, 4) < 0) {
        int saved = errno;
        unlink(path);
        errno = saved;
        goto fail;
    }

    return fd;

fail:
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }

    return -1;
}

int selyra_power_socket_poll(
    int server,
    enum selyra_power_action *action)
{
    if (!action) {
        errno = EINVAL;
        return -1;
    }

    *action = SELYRA_POWER_NONE;

    int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);

    if (client < 0) {
        if (errno == EINTR ||
            errno == EAGAIN ||
            errno == EWOULDBLOCK)
            return 0;

        return -1;
    }

    struct ucred cred;
    socklen_t len = sizeof(cred);

    int credentials_ok =
        getsockopt(client, SOL_SOCKET, SO_PEERCRED,
                   &cred, &len) == 0 &&
        len == sizeof(cred);

    const char *reply = "power: invalid command\n";
    int result = 0;

    if (!credentials_ok || ipc_set_timeout(client) < 0) {
        reply = "power: permission denied\n";
    } else {
        char request[32];

        ssize_t n = ipc_read_line(
            client, request, sizeof(request));

        int parsed = n > 0
            ? selyra_power_parse(request, cred.uid, action)
            : -1;

        if (parsed == -2) {
            reply = "power: permission denied\n";
        } else if (parsed == 0) {
            reply =
                "power: validated; shutdown not enabled yet\n";
            result = 1;
        }
    }

    (void)ipc_send_all(client, reply, strlen(reply));
    close(client);

    return result;
}
