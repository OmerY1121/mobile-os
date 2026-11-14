#define _GNU_SOURCE
#include "service_query.h"
#include "log_client.h"
#include <sys/prctl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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


static uid_t peer_uid(int fd)
{
    struct ucred credentials;
    socklen_t length = sizeof(credentials);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &credentials, &length) < 0 ||
        length != sizeof(credentials))
        return (uid_t)-1;

    return credentials.uid;
}

static int peer_allowed(int fd)
{
    uid_t uid = peer_uid(fd);

    return uid == geteuid() || uid == 0;
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

    if (mo_log_event("statusd: IPC ready") == 0)
        puts("statusd: boot event logged");

    struct timespec started;

    if (clock_gettime(CLOCK_MONOTONIC, &started) < 0) {
        perror("statusd: clock");
        close(server);
        return 1;
    }

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

        if (n > 0 && strcmp(request, "STOP\n") == 0) {
            if (peer_uid(client) != 0) {
                const char *denied =
                    "statusd: permission denied\n";

                (void)ipc_send_all(
                    client, denied, strlen(denied));
                close(client);
                continue;
            }

            const char *response =
                "statusd: stopping\n";

            (void)ipc_send_all(
                client, response, strlen(response));

            close(client);

            const char *path =
                getenv("MOBILE_OS_SOCKET");

            if (path == NULL || path[0] == '\0')
                path = "/run/mobile-os/status.sock";

            if (unlink(path) < 0)
                perror("statusd: unlink socket");

            close(server);
            puts("statusd: authorized shutdown");
            return 0;
        }

        char uptime_reply[64];
        char services_reply[256];
        const char *reply;

        if (n < 0) {
            reply = "statusd: invalid request\n";
        } else if (strcmp(request, "STATUS\n") == 0) {
            reply = "statusd: running\n";
        } else if (strcmp(request, "PING\n") == 0) {
            reply = "statusd: pong\n";
        } else if (strcmp(request, "UPTIME\n") == 0) {
            struct timespec now;

            if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                reply = "statusd: clock error\n";
            } else {
                long seconds =
                    (long)(now.tv_sec - started.tv_sec);

                snprintf(uptime_reply, sizeof(uptime_reply),
                         "statusd: uptime %lds\n", seconds);
                reply = uptime_reply;
            }
        } else if (strcmp(request, "SERVICES\n") == 0) {
            const char *path =
                getenv("MOBILE_OS_STATE_FILE");

            if (!path || !*path)
                path = "/run/mobile-os-state/services";

            if (mo_query_services(
                    path, services_reply,
                    sizeof(services_reply)) == 0)
                reply = services_reply;
            else
                reply = "statusd: state unavailable\n";
        } else if (strcmp(request, "HEALTH\n") == 0) {
            if (mo_log_event("statusd: health probe") == 0)
                reply = "health: statusd=ok logd=ok\n";
            else
                reply =
                    "health: statusd=ok logd=unavailable\n";
        } else {
            reply = "statusd: unknown command\n";
        }

        if (ipc_send_all(client, reply, strlen(reply)) < 0)
            perror("statusd: send");

        close(client);
    }
}
