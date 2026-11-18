#define _GNU_SOURCE
#include "config_engine.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static long long milliseconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;

    return (long long)ts.tv_sec * 1000 +
        ts.tv_nsec / 1000000;
}

static int wait_io(int fd, short events, long long deadline)
{
    for (;;) {
        long long now = milliseconds();

        if (now < 0)
            return -1;

        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        struct pollfd p = {
            .fd = fd,
            .events = events
        };

        int n = poll(&p, 1, (int)(deadline - now));

        if (n > 0)
            return 0;

        if (!n) {
            errno = ETIMEDOUT;
            return -1;
        }

        if (errno != EINTR)
            return -1;
    }
}

static int exchange(const char *path, const char *req,
                    char *out, size_t cap, int system_socket)
{
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };

    int n = snprintf(addr.sun_path,
                     sizeof(addr.sun_path), "%s", path);

    if (n <= 0 || (size_t)n >= sizeof(addr.sun_path)) {
        errno = EINVAL;
        return -1;
    }

    long long start = milliseconds();

    if (start < 0)
        return -1;

    long long deadline = start + 5000;

    int fd = socket(AF_UNIX,
        SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (fd < 0)
        return -1;

    int result = -1;

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0)
        goto done;

    struct ucred peer;
    socklen_t peer_size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &peer_size) < 0)
        goto done;

    uid_t expected = system_socket ? 1102 : geteuid();

    if (peer_size != sizeof(peer) || peer.uid != expected) {
        errno = EPERM;
        goto done;
    }

    size_t sent = 0, len = strlen(req), used = 0;

    while (sent < len) {
        if (wait_io(fd, POLLOUT, deadline) < 0)
            goto done;

        ssize_t k = send(fd, req + sent,
                         len - sent, MSG_NOSIGNAL);

        if (k < 0 && (errno == EINTR || errno == EAGAIN))
            continue;

        if (k <= 0) {
            if (!k)
                errno = EPIPE;
            goto done;
        }

        sent += (size_t)k;
    }

    while (used < cap - 1) {
        if (wait_io(fd, POLLIN, deadline) < 0)
            goto done;

        ssize_t k = recv(fd, out + used,
                         cap - 1 - used, 0);

        if (k < 0 && (errno == EINTR || errno == EAGAIN))
            continue;

        if (k < 0)
            goto done;

        if (!k) {
            out[used] = '\0';
            result = (int)used;
            goto done;
        }

        used += (size_t)k;
    }

    errno = EMSGSIZE;

done:
    {
        int saved = errno;
        close(fd);
        errno = saved;
    }

    return result;
}

static int token(const char *s)
{
    if (!*s)
        return 0;

    for (; *s; ++s)
        if ((unsigned char)*s < 33 ||
            (unsigned char)*s > 126)
            return 0;

    return 1;
}

static int valid_list(char *line)
{
    struct selyra_config cfg;
    selyra_config_defaults(&cfg);

    for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i) {
        const char *key = selyra_config_key(i);
        size_t n = strlen(key);
        char *end = strchr(line, '\n');

        if (!end || (size_t)(end - line) <= n + 1 ||
            strncmp(line, key, n) || line[n] != '=')
            return 0;

        *end = '\0';

        int ok = selyra_config_set(
            &cfg, key, line + n + 1) == 0;

        *end = '\n';

        if (!ok)
            return 0;

        line = end + 1;
    }

    return !*line;
}

static void usage(FILE *out)
{
    fputs("usage: configctl list | get KEY | set KEY VALUE | reset KEY | recovery | recover\n", out);
}

int main(int argc, char **argv)
{
    char req[160], response[1024];
    int n = -1;

    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }

    if (argc == 2 && !strcmp(argv[1], "list"))
        n = snprintf(req, sizeof(req), "LIST\n");

    else if (argc == 2 && !strcmp(argv[1], "recovery"))
        n = snprintf(req, sizeof(req), "RECOVERY\n");

    else if (argc == 2 && !strcmp(argv[1], "recover"))
        n = snprintf(req, sizeof(req), "RECOVER\n");

    else if (argc == 3 && token(argv[2])) {
        if (!strcmp(argv[1], "get"))
            n = snprintf(req, sizeof(req),
                         "GET %s\n", argv[2]);

        else if (!strcmp(argv[1], "reset"))
            n = snprintf(req, sizeof(req),
                         "RESET %s\n", argv[2]);

    } else if (argc == 4 && !strcmp(argv[1], "set") &&
               token(argv[2]) && token(argv[3]))

        n = snprintf(req, sizeof(req),
                     "SET %s %s\n", argv[2], argv[3]);

    if (n < 0 || (size_t)n >= sizeof(req)) {
        usage(stderr);
        return 2;
    }

    const char *path = getenv("SELYRA_CONFIG_SOCKET");

    if (!path || !*path)
        path = "/run/selyra-config/config.sock";

    n = exchange(path, req, response, sizeof(response),
                 !strcmp(path, "/run/selyra-config/config.sock"));

    if (n < 0) {
        perror("configctl: IPC");
        return 1;
    }

    if (!n || response[n - 1] != '\n')
        goto invalid;

    for (int i = 0; i < n; ++i)
        if (response[i] != '\n' &&
            ((unsigned char)response[i] < 32 ||
             (unsigned char)response[i] > 126))
            goto invalid;

    if (!strncmp(response, "ERR ", 4) &&
        strchr(response, '\n') == response + n - 1) {

        fprintf(stderr, "configctl: %s", response + 4);
        return 1;
    }

    if (!strcmp(argv[1], "list")) {
        if (strncmp(response, "OK\n", 3) ||
            !valid_list(response + 3))
            goto invalid;

        fputs(response + 3, stdout);

    } else if (!strcmp(argv[1], "get")) {
        if (strncmp(response, "OK ", 3) ||
            strchr(response, '\n') != response + n - 1)
            goto invalid;

        response[n - 1] = '\0';

        struct selyra_config cfg;
        selyra_config_defaults(&cfg);

        if (selyra_config_set(
                &cfg, argv[2], response + 3) < 0)
            goto invalid;

        puts(response + 3);

    } else if (!strcmp(argv[1], "recovery")) {
        if (strcmp(response, "OK required\n") &&
            strcmp(response, "OK not-required\n"))
            goto invalid;

        fputs(response + 3, stdout);

    } else {
        if (strcmp(response, "OK\n"))
            goto invalid;

        puts("OK");
    }

    return 0;

invalid:
    fputs("configctl: invalid server response\n", stderr);
    return 1;
}
