#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2 ||
        (strcmp(argv[1], "show") != 0 &&
         strcmp(argv[1], "write") != 0)) {
        fputs("usage: logctl show | "
              "logctl write MESSAGE\n", stderr);
        return 2;
    }

    char request[512];

    if (strcmp(argv[1], "show") == 0) {
        if (argc != 2)
            return 2;

        strcpy(request, "SHOW\n");
    } else {
        if (argc < 3)
            return 2;

        size_t used = 6;
        memcpy(request, "WRITE ", used);

        for (int i = 2; i < argc; ++i) {
            size_t n = strlen(argv[i]);

            if (used + n + 2 >= sizeof(request)) {
                fputs("logctl: message too long\n",
                      stderr);
                return 2;
            }

            if (i > 2)
                request[used++] = ' ';

            memcpy(request + used, argv[i], n);
            used += n;
        }

        request[used++] = '\n';
        request[used] = '\0';
    }

    const char *path = getenv("MOBILE_OS_LOG_SOCKET");

    if (!path || !*path)
        path = "/run/mobile-os-logs/log.sock";

    struct sockaddr_un address = {
        .sun_family = AF_UNIX
    };

    if (strlen(path) >= sizeof(address.sun_path))
        return 2;

    strcpy(address.sun_path, path);

    int fd = socket(AF_UNIX,
                    SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0 ||
        connect(fd, (struct sockaddr *)&address,
                sizeof(address)) < 0) {
        perror("logctl: connect");
        if (fd >= 0)
            close(fd);
        return 1;
    }

    size_t len = strlen(request);
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, request + sent,
                         len - sent, MSG_NOSIGNAL);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0) {
            perror("logctl: send");
            close(fd);
            return 1;
        }

        sent += (size_t)n;
    }

    shutdown(fd, SHUT_WR);

    char buf[4096];
    ssize_t n;
    int failed = 0;
    int first = 1;

    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        if (first && n >= 4 &&
            memcmp(buf, "ERR ", 4) == 0)
            failed = 1;

        first = 0;
        fwrite(buf, 1, (size_t)n, stdout);
    }

    if (n < 0) {
        perror("logctl: recv");
        failed = 1;
    }

    close(fd);
    return failed;
}
