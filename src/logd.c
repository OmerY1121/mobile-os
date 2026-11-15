#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "selyra_env.h"

#define MAX_REQUEST 512
#define MAX_LOG_BYTES (64 * 1024)

static volatile sig_atomic_t stopping;

static void on_stop(int sig)
{
    (void)sig;
    stopping = 1;
}

static int send_all(int fd, const char *p, size_t len)
{
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static void reply(int fd, const char *s)
{
    (void)send_all(fd, s, strlen(s));
}

static int read_request(int fd, char *buf, size_t cap)
{
    size_t used = 0;

    while (used + 1 < cap) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0)
            return -1;
        if (c == '\n') {
            buf[used] = '\0';
            return 0;
        }
        buf[used++] = c;
    }
    return -1;
}

static int peer_allowed(int fd)
{
    struct ucred peer;
    socklen_t size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &size) < 0 ||
        size != sizeof(peer))
        return 0;

    return peer.uid == geteuid() ||
           peer.uid == 0 ||
           peer.uid == 1100;
}

static int peer_can_read(int fd)
{
    struct ucred peer;
    socklen_t size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &size) < 0 ||
        size != sizeof(peer))
        return 0;

    return peer.uid == geteuid() || peer.uid == 0;
}

static int append_log(const char *path, const char *msg)
{
    if (!*msg)
        return -1;

    for (const unsigned char *p =
         (const unsigned char *)msg; *p; ++p) {
        if (*p < 32 || *p == 127)
            return -1;
    }

    time_t now = time(NULL);
    struct tm utc;
    char stamp[40];

    if (now == (time_t)-1 ||
        !gmtime_r(&now, &utc) ||
        !strftime(stamp, sizeof(stamp),
                  "%Y-%m-%dT%H:%M:%SZ", &utc))
        return -1;

    char entry[640];
    int length = snprintf(entry, sizeof(entry),
                          "%s %s\n", stamp, msg);

    if (length < 0 || (size_t)length >= sizeof(entry))
        return -1;

    int fd = open(path, O_WRONLY | O_CREAT |
                  O_APPEND | O_CLOEXEC | O_NOFOLLOW,
                  0600);

    if (fd < 0)
        return -1;

    struct stat st;

    if (fstat(fd, &st) < 0 ||
        !S_ISREG(st.st_mode) ||
        fchmod(fd, 0600) < 0) {
        close(fd);
        return -1;
    }

    if (st.st_size + length > MAX_LOG_BYTES) {
        close(fd);

        char old[sizeof(((struct sockaddr_un *)0)
                        ->sun_path) + 4];

        int n = snprintf(old, sizeof(old),
                         "%s.1", path);

        if (n < 0 || (size_t)n >= sizeof(old) ||
            rename(path, old) < 0)
            return -1;

        fd = open(path, O_WRONLY | O_CREAT |
                  O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                  0600);

        if (fd < 0)
            return -1;
    }

    size_t written = 0;
    while (written < (size_t)length) {
        ssize_t n = write(fd, entry + written,
                          (size_t)length - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fd);
            return -1;
        }
        written += (size_t)n;
    }

    return close(fd);
}

static int show_file(int client, const char *path)
{
    int fd = open(path, O_RDONLY |
                  O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0)
        return errno == ENOENT ? 0 : -1;

    char buf[4096];

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0) {
            close(fd);
            return -1;
        }

        if (n == 0)
            break;

        if (send_all(client, buf, (size_t)n) < 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}


static int bind_log_socket(
    int server, const struct sockaddr_un *address)
{
    socklen_t length = sizeof(*address);

    if (bind(server, (const struct sockaddr *)address,
             length) == 0)
        return 0;

    if (errno != EADDRINUSE)
        return -1;

    struct stat st;

    if (lstat(address->sun_path, &st) < 0)
        return -1;

    if (!S_ISSOCK(st.st_mode) ||
        st.st_uid != geteuid()) {
        errno = EADDRINUSE;
        return -1;
    }

    int probe = socket(AF_UNIX,
                       SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (probe < 0)
        return -1;

    int connected = connect(
        probe, (const struct sockaddr *)address, length);

    int error = errno;
    close(probe);

    if (connected == 0) {
        errno = EADDRINUSE;
        return -1;
    }

    if (error != ECONNREFUSED) {
        errno = error;
        return -1;
    }

    if (unlink(address->sun_path) < 0)
        return -1;

    puts("logd: removed stale socket");

    return bind(server,
                (const struct sockaddr *)address,
                length);
}

int main(void)
{
    const char *socket_path =
        selyra_env_value("SELYRA_LOG_SOCKET", "MOBILE_OS_LOG_SOCKET");
    const char *log_path =
        selyra_env_value("SELYRA_LOG_FILE", "MOBILE_OS_LOG_FILE");

    if (!socket_path || !*socket_path)
        socket_path = "/run/selyra-logs/log.sock";

    if (!log_path || !*log_path)
        log_path = "/run/selyra-logdata/current.log";

    struct sockaddr_un address = {
        .sun_family = AF_UNIX
    };

    if (strlen(socket_path) >= sizeof(address.sun_path) ||
        strlen(log_path) + 3 >= sizeof(address.sun_path)) {
        fputs("logd: path too long\n", stderr);
        return 1;
    }

    strcpy(address.sun_path, socket_path);
    umask(0077);

    int server = socket(AF_UNIX,
                        SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (server < 0 ||
        bind_log_socket(server, &address) < 0 ||
        listen(server, 8) < 0) {
        perror("logd: socket setup");
        if (server >= 0)
            close(server);
        return 1;
    }

    struct sigaction action = {
        .sa_handler = on_stop
    };

    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    if (chmod(socket_path, 0666) < 0) {
        perror("logd: socket permissions");
        close(server);
        unlink(socket_path);
        return 1;
    }

    puts("logd: local logging ready");
    fflush(stdout);

    while (!stopping) {
        int client = accept4(server, NULL, NULL,
                             SOCK_CLOEXEC);

        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("logd: accept");
            break;
        }

        struct timeval timeout = {.tv_sec = 2};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout));

        if (!peer_allowed(client)) {
            reply(client, "ERR permission denied\n");
        } else {
            char request[MAX_REQUEST];

            if (read_request(client, request,
                             sizeof(request)) != 0) {
                reply(client, "ERR invalid request\n");
            } else if (strncmp(request, "WRITE ", 6) == 0) {
                if (append_log(log_path, request + 6) == 0)
                    reply(client, "OK\n");
                else
                    reply(client, "ERR write failed\n");
            } else if (strcmp(request, "SHOW") == 0 &&
                       peer_can_read(client)) {
                char old[128];
                snprintf(old, sizeof(old),
                         "%s.1", log_path);

                if (show_file(client, old) < 0 ||
                    show_file(client, log_path) < 0)
                    reply(client, "ERR read failed\n");
            } else if (strcmp(request, "SHOW") == 0) {
                reply(client, "ERR permission denied\n");
            } else {
                reply(client, "ERR unknown command\n");
            }
        }

        close(client);
    }

    close(server);
    unlink(socket_path);
    return 0;
}
