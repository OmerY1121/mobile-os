#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "selyra_env.h"

int main(int argc, char **argv)
{
    if (argc < 2 ||
        (strcmp(argv[1], "show") != 0 &&
         strcmp(argv[1], "boot") != 0 &&
         strcmp(argv[1], "write") != 0)) {
        fputs("usage: logctl show | logctl boot | "
              "logctl write MESSAGE\n", stderr);
        return 2;
    }

    char request[512];

    if (strcmp(argv[1], "show") == 0 ||
        strcmp(argv[1], "boot") == 0) {
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

    const char *path = selyra_env_value("SELYRA_LOG_SOCKET", "MOBILE_OS_LOG_SOCKET");

    if (!path || !*path)
        path = "/run/selyra-logs/log.sock";

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
    char line[1024];
    size_t line_used = 0;

    int boot_mode = strcmp(argv[1], "boot") == 0;
    ssize_t n;
    int failed = 0;
    int first = 1;

    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        if (first && n >= 4 &&
            memcmp(buf, "ERR ", 4) == 0)
            failed = 1;

        first = 0;

        if (!boot_mode || failed) {
            if (fwrite(buf, 1, (size_t)n, stdout) !=
                (size_t)n)
                failed = 1;
            continue;
        }

        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                line[line_used] = '\0';

                if (strstr(line, " init: ") ||
                    strstr(line, " statusd: "))
                    puts(line);

                line_used = 0;
            } else if (line_used < sizeof(line) - 1) {
                line[line_used++] = buf[i];
            } else {
                fputs("logctl: log line too long\n",
                      stderr);
                failed = 1;
                break;
            }
        }
    }

    if (n < 0) {
        perror("logctl: recv");
        failed = 1;
    }

    close(fd);
    return failed;
}
