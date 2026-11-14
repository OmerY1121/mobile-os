#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int request(const char *command,
                   char *response, size_t capacity)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };

    strcpy(addr.sun_path,
           "/run/mobile-os-logs/log.sock");

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    size_t length = strlen(command);

    if (send(fd, command, length,
             MSG_NOSIGNAL) != (ssize_t)length) {
        close(fd);
        return -1;
    }

    shutdown(fd, SHUT_WR);

    ssize_t n = recv(fd, response, capacity - 1, 0);

    if (n <= 0) {
        close(fd);
        return -1;
    }

    response[n] = '\0';
    close(fd);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getuid() != 1100) {
        puts("FAIL: incorrect service UID");
        return 1;
    }

    char response[256];

    for (int i = 0; i < 100; ++i) {
        if (request("WRITE authorization-test\n",
                    response, sizeof(response)) == 0)
            break;

        usleep(50000);

        if (i == 99) {
            puts("FAIL: logd unavailable");
            return 1;
        }
    }

    if (strcmp(response, "OK\n") != 0) {
        puts("FAIL: statusd cannot write logs");
        return 1;
    }

    puts("PASS: statusd can write logs");

    if (request("SHOW\n", response,
                sizeof(response)) < 0) {
        puts("FAIL: read request failed");
        return 1;
    }

    if (strcmp(response,
               "ERR permission denied\n") != 0) {
        puts("FAIL: statusd can read logs");
        return 1;
    }

    puts("PASS: statusd cannot read logs");
    puts("All service log authorization checks passed.");

    for (;;)
        pause();
}
