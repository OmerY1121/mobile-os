#define _GNU_SOURCE
#include <sys/prctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "ipc_io.h"

static const char *socket_path(void)
{
    const char *env = getenv("MOBILE_OS_SOCKET");
    return (env && *env) ? env : "/run/mobile-os/status.sock";
}


static int peer_allowed(int fd)
{
    struct ucred credentials;
    socklen_t length = sizeof(credentials);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &length) < 0 ||
        length != sizeof(credentials)) {
        perror("statusd: peer credentials");
        return 0;
    }

    return credentials.uid == geteuid();
}

int main(void)
{
    const char *path = socket_path();
    struct sockaddr_un addr = {0};
    int server = socket(AF_UNIX, SOCK_STREAM, 0);

    if (server < 0) {
        perror("statusd: socket");
        return 1;
    }

    if (strlen(path) >= sizeof(addr.sun_path)) {
        fputs("statusd: socket path too long\n", stderr);
        close(server);
        return 1;
    }

    umask(0077);
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    if (unlink(path) < 0 && errno != ENOENT) {
        perror("statusd: unlink");
        close(server);
        return 1;
    }

    if (bind(server, (struct sockaddr *)&addr,
             sizeof(addr)) < 0 ||
        listen(server, 4) < 0) {
        perror("statusd: bind/listen");
        close(server);
        return 1;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    int nnp = prctl(PR_GET_NO_NEW_PRIVS,
                    0L, 0L, 0L, 0L);

    if (nnp < 0) {
        perror("statusd: get no_new_privs");
        close(server);
        return 1;
    }

    printf("statusd: no_new_privs=%d\n", nnp);
    puts("statusd: local IPC ready");

    for (;;) {
        int client = accept(server, NULL, NULL);

        if (client < 0) {
            if (errno == EINTR)
                continue;

            perror("statusd: accept");
            continue;
        }

        if (!peer_allowed(client)) {
            fputs("statusd: unauthorized peer\n", stderr);
            close(client);
            continue;
        }

        if (ipc_set_timeout(client) < 0) {
            perror("statusd: timeout setup");
            close(client);
            continue;
        }

        char request[32];
        ssize_t n = ipc_read_line(client, request,
                                  sizeof(request));

        const char *reply =
            n < 0 ? "statusd: invalid request\n" :
            strcmp(request, "STATUS\n") == 0 ?
            "statusd: running\n" :
            "statusd: unknown command\n";

        if (ipc_send_all(client, reply, strlen(reply)) < 0)
            perror("statusd: send");

        close(client);
    }
}
