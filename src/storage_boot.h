#ifndef SELYRA_STORAGE_BOOT_H
#define SELYRA_STORAGE_BOOT_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

enum storage_mode {
    STORAGE_DISABLED,
    STORAGE_READ_ONLY,
    STORAGE_READ_WRITE
};

static enum storage_mode storage_active =
    STORAGE_DISABLED;

static enum storage_mode storage_requested(void)
{
    char line[4096];
    FILE *file = fopen("/proc/cmdline", "r");

    if (!file)
        return STORAGE_DISABLED;

    int ok = fgets(line, sizeof(line), file) != NULL;
    fclose(file);

    if (!ok || !strchr(line, '\n'))
        return STORAGE_DISABLED;

    char *save = NULL;
    enum storage_mode mode = STORAGE_DISABLED;

    for (char *arg = strtok_r(line, " \t\n", &save);
         arg; arg = strtok_r(NULL, " \t\n", &save)) {

        if (strncmp(arg, "selyra.storage=", 15) != 0)
            continue;

        if (mode != STORAGE_DISABLED)
            return STORAGE_DISABLED;

        if (strcmp(arg, "selyra.storage=ro") == 0)
            mode = STORAGE_READ_ONLY;
        else if (strcmp(arg, "selyra.storage=rw") == 0)
            mode = STORAGE_READ_WRITE;
        else
            return STORAGE_DISABLED;
    }

    return mode;
}

static int storage_mount(enum storage_mode mode)
{
    if (mount("devtmpfs", "/dev", "devtmpfs",
              MS_NOSUID | MS_NOEXEC, "mode=0755") < 0) {
        perror("init: storage devtmpfs");
        return -1;
    }

    struct stat st;
    int fd = -1;

    for (int i = 0; i < 30; ++i) {
        fd = open("/dev/vda",
                  O_RDONLY | O_CLOEXEC |
                  O_NOFOLLOW | O_NONBLOCK);

        if (fd >= 0)
            break;

        usleep(100000);
    }

    if (fd < 0) {
        perror("init: storage disk unavailable");
        return -1;
    }

    static const unsigned char label[16] = "SELYRA_DATA";
    unsigned char sb[1024];

    int valid =
        fstat(fd, &st) == 0 &&
        S_ISBLK(st.st_mode) &&
        pread(fd, sb, sizeof(sb), 1024) ==
            (ssize_t)sizeof(sb) &&
        sb[56] == 0x53 &&
        sb[57] == 0xef &&
        memcmp(sb + 120, label, sizeof(label)) == 0;

    close(fd);

    if (!valid) {
        fputs("init: storage signature or label invalid\n",
              stderr);
        return -1;
    }

    if ((mkdir("/data", 0700) < 0 && errno != EEXIST) ||
        lstat("/data", &st) < 0 ||
        !S_ISDIR(st.st_mode) ||
        st.st_uid != 0 ||
        (st.st_mode & 0777) != 0700) {

        fputs("init: invalid /data mount point\n", stderr);
        return -1;
    }

    unsigned long flags =
        MS_NODEV | MS_NOSUID | MS_NOEXEC;

    const char *options;

    if (mode == STORAGE_READ_ONLY) {
        flags |= MS_RDONLY;
        options = "noload";
    } else {
        options = "errors=remount-ro";
    }

    if (mount("/dev/vda", "/data", "ext4",
              flags, options) < 0) {
        perror("init: storage mount");
        return -1;
    }

    if (lstat("/data", &st) < 0 ||
        !S_ISDIR(st.st_mode) ||
        st.st_uid != 0 ||
        st.st_gid != 0 ||
        (st.st_mode & 07777) != 0700) {

        fputs("init: storage root permissions invalid\\n",
              stderr);

        if (umount("/data") < 0 &&
            umount2("/data", MNT_DETACH) < 0) {
            perror("init: unsafe storage unmount");
            for (;;) pause();
        }

        return -1;
    }

    struct statvfs info;

    if (statvfs("/data", &info) < 0 ||
        (!!(info.f_flag & ST_RDONLY) !=
         (mode == STORAGE_READ_ONLY))) {

        fputs("init: unexpected storage mount mode\n", stderr);
        umount("/data");
        return -1;
    }

    storage_active = mode;

    if (mode == STORAGE_READ_ONLY)
        puts("init: storage mounted read-only at /data");
    else
        puts("init: storage mounted read-write at /data");

    return 0;
}

static void prepare_boot_storage(void)
{
    enum storage_mode mode = storage_requested();

    if (mode == STORAGE_DISABLED) {
        puts("init: storage disabled");
        return;
    }

    if (storage_mount(mode) < 0)
        puts("init: storage unavailable; continuing without persistent data");
}


#ifndef SELYRA_POWER_DRY_RUN
static int storage_shutdown(void)
{
    if (storage_active == STORAGE_DISABLED)
        return 0;

    if (storage_active == STORAGE_READ_WRITE) {
        int fd = open("/data",
                      O_RDONLY | O_DIRECTORY |
                      O_NOFOLLOW | O_CLOEXEC);

        if (fd < 0)
            return -1;

        if (syncfs(fd) < 0) {
            int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }

        if (close(fd) < 0)
            return -1;

        puts("init: storage synchronized");
    }

    if (umount("/data") < 0)
        return -1;

    storage_active = STORAGE_DISABLED;

    puts("init: storage unmounted safely");

    return 0;
}
#endif

#endif
