#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static const char *socket_path(void)
{
    const char *env = getenv("MOBILE_OS_SOCKET");
    return (env && *env) ? env : "/run/mobile-os/status.sock";
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

    signal(SIGPIPE, SIG_IGN);
    umask(0077);

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    unlink(path);

    if (bind(server, (struct sockaddr *)&addr,
             sizeof(addr)) < 0 ||
        listen(server, 4) < 0) {
        perror("statusd: bind/listen");
        close(server);
        return 1;
    }

    puts("statusd: local IPC ready");
    fflush(stdout);

    for (;;) {
        int client = accept(server, NULL, NULL);

        if (client < 0) {
            if (errno == EINTR)
                continue;

            perror("statusd: accept");
            continue;
        }

        char request[32];
        ssize_t n = read(client, request,
                         sizeof(request));

        const char *answer =
            (n == 7 &&
             memcmp(request, "STATUS\n", 7) == 0)
            ? "statusd: running\n"
            : "statusd: unknown command\n";

        if (write(client, answer, strlen(answer)) < 0) {
            perror("statusd: write");
        }
        close(client);
    }
}
