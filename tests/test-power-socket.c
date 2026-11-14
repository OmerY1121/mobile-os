#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "../src/power_socket.h"

static int exchange(
    int server,
    const char *path,
    const char *request,
    int expected_result,
    const char *expected_reply)
{
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    int client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0)
        return -1;

    if (connect(client, (struct sockaddr *)&addr,
                sizeof(addr)) < 0 ||
        send(client, request, strlen(request), 0) !=
            (ssize_t)strlen(request)) {
        close(client);
        return -1;
    }

    enum selyra_power_action action = SELYRA_POWER_NONE;
    int result = selyra_power_socket_poll(server, &action);

    char received[128];
    ssize_t n = recv(client, received,
                     sizeof(received) - 1, 0);
    close(client);

    if (n <= 0)
        return -1;

    received[n] = '\0';

    if (result != expected_result ||
        strcmp(received, expected_reply) != 0)
        return -1;

    if ((result == 1) != (action != SELYRA_POWER_NONE))
        return -1;

    return 0;
}

int main(void)
{
    char dir[] = "/tmp/selyra-power-XXXXXX";

    if (!mkdtemp(dir))
        return 1;

    char path[128];
    snprintf(path, sizeof(path),
             "%s/control.sock", dir);

    int server = selyra_power_socket_open(path);

    if (server < 0) {
        perror("power socket setup");
        return 1;
    }

    struct stat st;

    if (stat(path, &st) < 0 ||
        (st.st_mode & 0777) != 0600)
        return 1;

    int is_root = geteuid() == 0;

    const char *valid_reply = is_root
        ? "power: validated; shutdown not enabled yet\n"
        : "power: permission denied\n";

    const char *invalid_reply = is_root
        ? "power: invalid command\n"
        : "power: permission denied\n";

    if (exchange(server, path, "REBOOT\n",
                 is_root, valid_reply) != 0 ||
        exchange(server, path, "POWEROFF\n",
                 is_root, valid_reply) != 0 ||
        exchange(server, path, "INVALID\n",
                 0, invalid_reply) != 0) {
        fputs("FAIL: power socket transport/auth\n", stderr);
        return 1;
    }

    close(server);
    unlink(path);
    rmdir(dir);

    puts("PASS: socket permissions, peer credentials, protocol transport");
    puts("PASS: no shutdown performed");

    return 0;
}
