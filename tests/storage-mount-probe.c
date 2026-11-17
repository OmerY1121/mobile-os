#define _GNU_SOURCE
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <unistd.h>

static void stop(const char *reason)
{
    perror(reason);
    printf("FAIL: %s\n", reason);
    for (;;) pause();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getpid() != 1) {
        puts("FAIL: probe must run as PID 1");
        for (;;) pause();
    }

    if (mount("devtmpfs", "/dev", "devtmpfs",
              MS_NOSUID | MS_NOEXEC, "mode=0755") < 0)
        stop("mount devtmpfs");

    puts("PASS: devtmpfs mounted");

    struct stat device;
    int found = 0;

    for (int i = 0; i < 100; ++i) {
        if (stat("/dev/vda", &device) == 0 &&
            S_ISBLK(device.st_mode)) {
            found = 1;
            break;
        }
        usleep(100000);
    }

    if (!found) {
        puts("FAIL: /dev/vda not available");
        for (;;) pause();
    }

    puts("PASS: virtio disk available at /dev/vda");

    if (mount("/dev/vda", "/data", "ext4",
              MS_RDONLY | MS_NODEV |
              MS_NOSUID | MS_NOEXEC, NULL) < 0)
        stop("mount ext4 read-only");

    struct statfs fs;

    if (statfs("/data", &fs) < 0 ||
        fs.f_type != EXT4_SUPER_MAGIC) {
        puts("FAIL: filesystem is not ext4");
        for (;;) pause();
    }

    puts("PASS: ext4 mounted at /data");

    struct statvfs info;

    if (statvfs("/data", &info) < 0 ||
        !(info.f_flag & ST_RDONLY)) {
        puts("FAIL: /data is not read-only");
        for (;;) pause();
    }

    puts("PASS: /data is read-only");
    puts("All Selyra read-only storage checks passed.");

    for (;;) pause();
}
