#ifndef SELYRA_CONFIG_BOOT_H
#define SELYRA_CONFIG_BOOT_H

#include "config_store.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIGD_UID 1102
#define CONFIGD_GID 1102
#define CONFIGD_RUNTIME "/run/selyra-config"
#define CONFIGD_DATA "/run/selyra-config-data"

static int prepare_config_directory(const char *path)
{
    if (mkdir(path, 0700) < 0 && errno != EEXIST)
        return -1;

    int fd = open(path, O_RDONLY | O_DIRECTORY |
                  O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0)
        return -1;

    struct stat st;

    int ok = fstat(fd, &st) == 0 &&
        (st.st_uid == 0 || st.st_uid == CONFIGD_UID) &&
        (st.st_mode & 07777) == 0700 &&
        fchown(fd, CONFIGD_UID, CONFIGD_GID) == 0 &&
        fchmod(fd, 0700) == 0;

    close(fd);

    return ok ? 0 : -1;
}


static int config_directory_fd(int *readonly)
{
    *readonly = storage_active == STORAGE_READ_ONLY;

    if (storage_active == STORAGE_DISABLED) {
        puts("init: volatile configuration active");
        return open(CONFIGD_DATA, O_RDONLY | O_DIRECTORY |
                    O_NOFOLLOW | O_CLOEXEC);
    }

    int parent = open("/data", O_RDONLY | O_DIRECTORY |
                      O_NOFOLLOW | O_CLOEXEC);

    if (parent < 0)
        return -1;

    int fd = openat(parent, "config", O_RDONLY |
                    O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

    if (fd < 0 && errno == ENOENT) {
        if (*readonly) {
            close(parent);

            puts("init: read-only configuration defaults");

            return open(CONFIGD_DATA, O_RDONLY |
                        O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }

        if (mkdirat(parent, "config", 0700) < 0) {
            close(parent);
            return -1;
        }

        fd = openat(parent, "config", O_RDONLY |
                    O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

        if (fd < 0 ||
            fchown(fd, CONFIGD_UID, CONFIGD_GID) < 0 ||
            fchmod(fd, 0700) < 0 ||
            fsync(fd) < 0 || fsync(parent) < 0) {

            if (fd >= 0)
                close(fd);

            close(parent);
            return -1;
        }
    }

    close(parent);

    if (fd < 0)
        return -1;

    struct stat st;

    if (fstat(fd, &st) < 0 ||
        st.st_uid != CONFIGD_UID ||
        st.st_gid != CONFIGD_GID ||
        (st.st_mode & 07777) != 0700) {
        close(fd);
        return -1;
    }

    puts("init: persistent configuration active");

    return fd;
}


static char boot_log_limit[SELYRA_CONFIG_VALUE_SIZE] = "64";

static void apply_boot_configuration(void)
{
    struct selyra_config cfg;
    selyra_config_defaults(&cfg);

    int readonly;
    int fd = config_directory_fd(&readonly);

    if (fd >= 0) {
        int result = selyra_config_load_owned(
            fd, &cfg, CONFIGD_UID);

        close(fd);

        if (result == CONFIG_STORE_OK)
            puts("init: saved system settings loaded");
        else
            puts("init: default system settings active");
    } else {
        puts("init: configuration unavailable; using defaults");
    }

    const char *hostname =
        selyra_config_get(&cfg, "system.hostname");

    if (sethostname(hostname, strlen(hostname)) < 0)
        perror("init: sethostname");
    else
        printf("init: hostname=%s\n", hostname);

    snprintf(boot_log_limit, sizeof(boot_log_limit),
             "%s", selyra_config_get(&cfg, "logging.max_kib"));

    printf("init: log limit=%s KiB\n", boot_log_limit);
}

static void launch_configd(void)
{
    int readonly;
    int dir = config_directory_fd(&readonly);

    if (dir < 0 || fcntl(dir, F_SETFD, 0) < 0) {
        perror("init: config directory");
        _exit(126);
    }

    char argument[48];
    snprintf(argument, sizeof(argument),
             "--dir-fd=%d", dir);

    if (setgroups(0, NULL) < 0 ||
        setgid(CONFIGD_GID) < 0 ||
        setuid(CONFIGD_UID) < 0 ||
        prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) < 0)
        _exit(126);

    struct stat st;
    const char *socket = CONFIGD_RUNTIME "/config.sock";

    if (lstat(socket, &st) == 0) {
        if (!S_ISSOCK(st.st_mode) ||
            st.st_uid != CONFIGD_UID ||
            unlink(socket) < 0)
            _exit(126);
    } else if (errno != ENOENT) {
        _exit(126);
    }

    execl("/bin/configd", "configd",
          argument, CONFIGD_RUNTIME,
          readonly ? "ro" : "rw",
          (char *)NULL);

    perror("init: exec configd");
    _exit(127);
}

#endif
