#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stdio.h>
#include <sys/statfs.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getuid() != 1100 || getgid() != 1100) {
        puts("FAIL: wrong service identity");
        return 1;
    }

    struct statfs fs;

    if (statfs("/tmp", &fs) < 0 ||
        fs.f_type != TMPFS_MAGIC) {
        puts("FAIL: /tmp is not a tmpfs");
        return 1;
    }

    puts("PASS: /tmp uses tmpfs");

    if (access("/tmp/parent-marker", F_OK) == 0 ||
        errno != ENOENT) {
        puts("FAIL: original /tmp is still visible");
        return 1;
    }

    puts("PASS: parent /tmp marker is hidden");

    int fd = open("/tmp/statusd-private",
                  O_WRONLY | O_CREAT | O_EXCL, 0600);

    if (fd < 0) {
        perror("FAIL: cannot write private /tmp");
        return 1;
    }

    close(fd);

    puts("PASS: private /tmp is writable");
    puts("All mount isolation checks passed.");

    for (;;)
        pause();
}
