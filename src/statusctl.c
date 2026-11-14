#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ipc_io.h"

int main(int argc, char **argv)
{
    const char *request = "STATUS\n";

    if (argc > 2) {
        fputs("usage: statusctl [status|ping|uptime|stop|services]\n",
              stderr);
        return 1;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "status") == 0)
            request = "STATUS\n";
        else if (strcmp(argv[1], "ping") == 0)
            request = "PING\n";
        else if (strcmp(argv[1], "uptime") == 0)
            request = "UPTIME\n";
        else if (strcmp(argv[1], "services") == 0)
            request = "SERVICES\n";
        else if (strcmp(argv[1], "stop") == 0)
            request = "STOP\n";
        else {
            fprintf(stderr, "Unknown command: %s\n", argv[1]);
            return 1;
        }
    }

    const char *env = getenv("MOBILE_OS_SOCKET");
    const char *path = (env && *env)
        ? env : "/run/mobile-os/status.sock";

    struct sockaddr_un addr = {0};

    if (strlen(path) >= sizeof(addr.sun_path)) {
        fputs("statusctl: socket path too long\n", stderr);
        return 1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        perror("statusctl: socket");
        return 1;
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    if (ipc_set_timeout(fd) < 0 ||
        connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("statusctl: connect");
        close(fd);
        return 1;
    }

    if (ipc_send_all(fd, request, strlen(request)) < 0) {
        perror("statusctl: send");
        close(fd);
        return 1;
    }

    char reply[128];
    ssize_t n = ipc_read_line(fd, reply, sizeof(reply));

    if (n < 0) {
        perror("statusctl: receive");
        close(fd);
        return 1;
    }

    fwrite(reply, 1, (size_t)n, stdout);
    close(fd);
    return 0;
}
