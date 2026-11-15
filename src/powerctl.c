#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "ipc_io.h"

#ifndef SELYRA_POWER_SOCKET_PATH
#define SELYRA_POWER_SOCKET_PATH "/run/selyra-power/control.sock"
#endif

int main(int argc, char **argv)
{
    const char *request;

    if (argc != 2) {
        fputs("usage: powerctl [poweroff|reboot]\n", stderr);
        return 1;
    }

    if (strcmp(argv[1], "poweroff") == 0)
        request = "POWEROFF\n";
    else if (strcmp(argv[1], "reboot") == 0)
        request = "REBOOT\n";
    else {
        fputs("powerctl: unknown action\n", stderr);
        return 1;
    }

    const char *path = SELYRA_POWER_SOCKET_PATH;

#ifdef SELYRA_POWERCTL_TEST
    const char *override = getenv("SELYRA_POWER_SOCKET");
    if (override && *override)
        path = override;
#endif

    struct sockaddr_un addr = {0};

    if (strlen(path) >= sizeof(addr.sun_path)) {
        fputs("powerctl: socket path too long\n", stderr);
        return 1;
    }

    int fd = socket(AF_UNIX,
                    SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0) {
        perror("powerctl: socket");
        return 1;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    if (ipc_set_timeout(fd) != 0 ||
        connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) != 0) {
        perror("powerctl: connect");
        close(fd);
        return 1;
    }

    if (ipc_send_all(fd, request, strlen(request)) != 0) {
        perror("powerctl: send");
        close(fd);
        return 1;
    }

    char reply[128];
    ssize_t n = ipc_read_line(fd, reply, sizeof(reply));
    close(fd);

    if (n < 0) {
        perror("powerctl: receive");
        return 1;
    }

    if (fwrite(reply, 1, (size_t)n, stdout) != (size_t)n) {
        perror("powerctl: output");
        return 1;
    }

    if (strcmp(reply,
        "power: accepted; shutdown in progress\n") == 0)
        return 0;


    return 2;
}
