#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(void)
{
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

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("statusctl: connect");
        close(fd);
        return 1;
    }

    if (write(fd, "STATUS\n", 7) != 7) {
        perror("statusctl: write");
        close(fd);
        return 1;
    }

    char reply[128];
    ssize_t n = read(fd, reply, sizeof(reply));

    if (n <= 0) {
        fputs("statusctl: no response\n", stderr);
        close(fd);
        return 1;
    }

    fwrite(reply, 1, (size_t)n, stdout);

    close(fd);
    return 0;
}
