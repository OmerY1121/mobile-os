#define _GNU_SOURCE
#include "config_store.h"
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void stop(int sig)
{
    (void)sig;
    stopping = 1;
}

static int private_dir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY |
                  O_NOFOLLOW | O_CLOEXEC);

    struct stat st;

    if (fd < 0)
        return -1;

    if (fstat(fd, &st) < 0 ||
        st.st_uid != geteuid() ||
        (st.st_mode & 07777) != 0700) {
        close(fd);
        return -1;
    }

    return fd;
}

static long long milliseconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;

    return (long long)ts.tv_sec * 1000 +
        ts.tv_nsec / 1000000;
}

static int read_request(int fd, char *buf, size_t cap)
{
    long long start = milliseconds();

    if (start < 0)
        return -1;

    for (size_t i = 0; i + 1 < cap;) {
        long long now = milliseconds();

        if (stopping || now < 0 || now - start >= 2000)
            return -1;

        struct pollfd p = {
            .fd = fd,
            .events = POLLIN
        };

        int ready = poll(&p, 1,
                         (int)(2000 - (now - start)));

        if (ready < 0 && errno == EINTR)
            continue;

        if (ready <= 0)
            return -1;

        unsigned char c;
        ssize_t n = recv(fd, &c, 1, MSG_DONTWAIT);

        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;

        if (n != 1)
            return -1;

        if (c == '\n') {
            buf[i] = '\0';
            return 0;
        }

        if (c < 32 || c > 126)
            return -1;

        buf[i++] = (char)c;
    }

    return -1;
}

static void reply(int fd, const char *text)
{
    size_t left = strlen(text);

    while (left) {
        ssize_t n = send(fd, text, left,
                         MSG_NOSIGNAL | MSG_DONTWAIT);

        if (n < 0 && errno == EINTR && !stopping)
            continue;

        if (n <= 0)
            return;

        text += n;
        left -= (size_t)n;
    }
}

static void handle(int fd, int dir,
                   struct selyra_config *cfg, int *recovering,
                   int readonly)
{
    struct ucred peer;
    socklen_t size = sizeof(peer);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED,
                   &peer, &size) < 0 ||
        size != sizeof(peer) ||
        (peer.uid != 0 && peer.uid != geteuid())) {
        reply(fd, "ERR denied\n");
        return;
    }

    char request[160], out[256];

    if (read_request(fd, request, sizeof(request)) < 0) {
        reply(fd, "ERR invalid request\n");
        return;
    }

    if (!strcmp(request, "LIST")) {
        reply(fd, "OK\n");

        for (unsigned i = 0; i < SELYRA_CONFIG_COUNT; ++i) {
            const char *key = selyra_config_key(i);

            snprintf(out, sizeof(out), "%s=%s\n", key,
                     selyra_config_get(cfg, key));

            reply(fd, out);
        }

        return;
    }

    if (!strncmp(request, "GET ", 4)) {
        const char *value =
            selyra_config_get(cfg, request + 4);

        if (!value) {
            reply(fd, "ERR unknown key\n");
            return;
        }

        snprintf(out, sizeof(out), "OK %s\n", value);
        reply(fd, out);

        return;
    }

    if (!strcmp(request, "RECOVERY")) {
        reply(fd, *recovering ?
              "OK required\n" : "OK not-required\n");
        return;
    }

    if (readonly) {
        reply(fd, "ERR read-only configuration\n");
        return;
    }

    int recover = !strcmp(request, "RECOVER");

    if (*recovering && !recover) {
        reply(fd, "ERR recovery required\n");
        return;
    }

    if (recover && !*recovering) {
        reply(fd, "ERR recovery not required\n");
        return;
    }

    struct selyra_config next = *cfg;
    int valid = -1;

    if (recover) {
        selyra_config_defaults(&next);
        valid = 0;
    } else if (!strncmp(request, "SET ", 4)) {
        char *key = request + 4;
        char *value = strchr(key, ' ');

        if (value) {
            *value++ = '\0';
            valid = selyra_config_set(&next, key, value);
        }

    } else if (!strncmp(request, "RESET ", 6)) {
        valid = selyra_config_reset(&next, request + 6);
    }

    if (valid < 0) {
        reply(fd, "ERR invalid setting\n");
        return;
    }

    int saved = selyra_config_save(dir, &next);

    if (saved == CONFIG_STORE_OK ||
        saved == CONFIG_STORE_UNCERTAIN) {
        *cfg = next;
        *recovering = 0;
    }

    reply(fd, saved == CONFIG_STORE_OK ? "OK\n" :
          saved == CONFIG_STORE_UNCERTAIN ?
          "ERR durability uncertain\n" :
          "ERR save failed\n");
}

int main(int argc, char **argv)
{
    if (argc != 3 &&
        !(argc == 4 && (!strcmp(argv[3], "ro") ||
                        !strcmp(argv[3], "rw")))) {
        fputs("usage: configd CONFIG_DIR SOCKET_DIR\n", stderr);
        return 1;
    }

    umask(0077);

    int dir = -1;

    if (!strncmp(argv[1], "--dir-fd=", 9)) {
        char *end;
        errno = 0;

        long n = strtol(argv[1] + 9, &end, 10);

        if (!errno && end != argv[1] + 9 &&
            !*end && n >= 3 && n <= INT_MAX) {

            struct stat st;

            if (fstat((int)n, &st) == 0 &&
                S_ISDIR(st.st_mode) &&
                st.st_uid == geteuid() &&
                (st.st_mode & 07777) == 0700 &&
                fcntl((int)n, F_SETFD, FD_CLOEXEC) == 0)
                dir = (int)n;
        }
    } else {
        dir = private_dir(argv[1]);
    }
    int runtime = private_dir(argv[2]);

    if (dir < 0 || runtime < 0) {
        fputs("configd: unsafe directory\n", stderr);
        return 1;
    }

    struct statvfs info;

    if (fstatvfs(dir, &info) < 0)
        return 1;

    int readonly = (info.f_flag & ST_RDONLY) ||
        (argc == 4 && !strcmp(argv[3], "ro"));

    struct selyra_config cfg;
    selyra_config_defaults(&cfg);

    int loaded = selyra_config_load(dir, &cfg);

    int recovering = loaded == CONFIG_STORE_INVALID;

    if (recovering)
        fputs("configd: recovery mode active\n", stderr);

    if (!recovering && loaded != CONFIG_STORE_OK &&
        loaded != CONFIG_STORE_MISSING) {
        fputs("configd: invalid configuration\n", stderr);
        return 1;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };

    int n = snprintf(addr.sun_path, sizeof(addr.sun_path),
                     "%s/config.sock", argv[2]);

    if (n < 0 || (size_t)n >= sizeof(addr.sun_path))
        return 1;

    struct sigaction sa = { .sa_handler = stop };
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) < 0 ||
        sigaction(SIGINT, &sa, NULL) < 0)
        return 1;

    int server = socket(AF_UNIX,
        SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (server < 0 ||
        bind(server, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("configd: socket");
        return 1;
    }

    int result = 0;

    if (listen(server, 8) < 0) {
        perror("configd: listen");
        result = 1;
    } else {
        puts("configd: local IPC ready");
        fflush(stdout);

        while (!stopping) {
            struct pollfd p = {
                .fd = server,
                .events = POLLIN
            };

            int ready = poll(&p, 1, 200);

            if (!ready || (ready < 0 && errno == EINTR))
                continue;

            if (ready < 0) {
                result = 1;
                break;
            }

            int client = accept4(server, NULL, NULL,
                                 SOCK_CLOEXEC);

            if (client < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;

                result = 1;
                break;
            }

            handle(client, dir, &cfg, &recovering, readonly);
            close(client);
        }
    }

    unlinkat(runtime, "config.sock", 0);

    close(server);
    close(runtime);
    close(dir);

    return result;
}
