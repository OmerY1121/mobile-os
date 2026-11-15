#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/selyra-auth.sock"

static int attempt(uid_t uid, int expected_allowed)
{
    pid_t child = fork();
    if (child < 0) return -1;

    if (child == 0) {
        if (setgid((gid_t)uid) != 0 || setuid(uid) != 0)
            _exit(10);

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) _exit(11);

        struct timeval timeout = {.tv_sec = 3};
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) != 0)
            _exit(12);

        struct sockaddr_un address = {
            .sun_family = AF_UNIX
        };
        strcpy(address.sun_path, SOCKET_PATH);

        if (connect(fd, (struct sockaddr *)&address,
                    sizeof(address)) != 0)
            _exit(13);

        ssize_t sent = send(
            fd, "STATUS\n", 7, MSG_NOSIGNAL);

        char reply[128];
        ssize_t got = sent == 7
            ? recv(fd, reply, sizeof(reply), 0) : -1;

        int result;

        if (expected_allowed) {
            result =
                got == (ssize_t)strlen("statusd: running\n") &&
                memcmp(reply, "statusd: running\n",
                       (size_t)got) == 0;
        } else {
            result =
                (sent == -1 &&
                 (errno == EPIPE || errno == ECONNRESET)) ||
                got == 0 ||
                (got == -1 && errno == ECONNRESET);
        }

        close(fd);
        _exit(result ? 0 : 14);
    }

    int status;
    if (waitpid(child, &status, 0) < 0)
        return -1;

    return WIFEXITED(status)
        ? WEXITSTATUS(status) : -1;
}

static void report(const char *label, int result)
{
    printf("%s: %s",
           result == 0 ? "PASS" : "FAIL", label);

    if (result != 0)
        printf(" (exit=%d)", result);

    putchar('\n');
    fflush(stdout);

    if (result != 0)
        for (;;) pause();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getpid() != 1 || geteuid() != 0) {
        puts("FAIL: guest test must be root PID 1");
        for (;;) pause();
    }

    mkdir("/tmp", 01777);
    chmod("/tmp", 01777);
    unlink(SOCKET_PATH);

    pid_t server = fork();

    if (server < 0) {
        puts("FAIL: cannot fork statusd");
        for (;;) pause();
    }

    if (server == 0) {
        if (setgid(1100) != 0 || setuid(1100) != 0)
            _exit(16);

        setenv("MOBILE_OS_SOCKET", SOCKET_PATH, 1);
        execl("/bin/statusd", "statusd", (char *)NULL);
        _exit(17);
    }

    int ready = 0;

    for (int i = 0; i < 100; ++i) {
        struct stat st;

        if (stat(SOCKET_PATH, &st) == 0 &&
            S_ISSOCK(st.st_mode)) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    report("statusd socket created", ready ? 0 : 1);

    report("test socket accessible to both UIDs",
           chmod(SOCKET_PATH, 0777));

    report("root connection accepted",
           attempt(0, 1));

    report("service owner connection accepted",
           attempt(1100, 1));

    report("other UID rejected after connecting",
           attempt(1200, 0));

    report("authorized connection still works",
           attempt(1100, 1));

    report("statusd remains alive", kill(server, 0));

    puts("All QEMU IPC peer authorization checks passed.");

    for (;;) pause();
}
