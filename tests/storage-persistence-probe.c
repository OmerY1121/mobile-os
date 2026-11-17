#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <unistd.h>

#define FILE_PATH "/data/persistence.txt"

static const char payload[] =
    "Selyra OS persistent storage test\n";

static void fail(const char *message)
{
    fprintf(stderr, "STORAGE-FAIL: %s\n", message);
    _exit(1);
}


static int access_denied(const char *path, int flags)
{
    int fd = open(path, flags, 0600);

    if (fd >= 0) {
        close(fd);
        return 0;
    }

    return errno == EACCES || errno == EPERM;
}

static void test_storage_security(void)
{
    struct stat st;
    struct statvfs info;

    if (stat("/data", &st) < 0)
        fail("Cannot inspect data directory");

    printf("DATA-INFO: owner=%u mode=%04o\n",
           (unsigned)st.st_uid,
           (unsigned)(st.st_mode & 07777));

    if (st.st_uid != 0 || st.st_gid != 0 ||
        (st.st_mode & 07777) != 0700)
        fail("Unsafe data directory ownership or permissions");

    unsigned long required =
        ST_NODEV | ST_NOSUID | ST_NOEXEC;

    if (statvfs("/data", &info) < 0 ||
        (info.f_flag & required) != required)
        fail("Unsafe storage mount flags");

    puts("PASS: Secure storage mount flags");

    uid_t users[] = {1000, 1100, 1101, 1200};

    for (size_t i = 0;
         i < sizeof(users) / sizeof(users[0]); ++i) {

        pid_t child = fork();

        if (child < 0)
            fail("Cannot fork permission probe");

        if (child == 0) {
            if (setgroups(0, NULL) < 0 ||
                setgid(users[i]) < 0 ||
                setuid(users[i]) < 0)
                _exit(10);

            if (!access_denied(FILE_PATH, O_RDONLY))
                _exit(11);

            if (!access_denied(
                    "/data/unauthorized.txt",
                    O_WRONLY | O_CREAT | O_EXCL))
                _exit(12);

            if (!access_denied("/dev/vda", O_RDONLY))
                _exit(13);

            if (!access_denied(
                    "/data", O_RDONLY | O_DIRECTORY))
                _exit(14);

            _exit(0);
        }

        int status;

        if (waitpid(child, &status, 0) < 0 ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {

            fprintf(stderr,
                    "STORAGE-FAIL: UID %u access check\n",
                    (unsigned)users[i]);

            fail("Storage permission test failed");
        }

        printf("PASS: Unauthorized UID %u rejected\n",
               (unsigned)users[i]);
    }

    puts("PASS: Storage permission checks completed");
}

static void probe(void)
{
    int ready = 0;

    for (int i = 0; i < 300; ++i) {
        if (access("/run/selyra-power/control.sock",
                   F_OK) == 0 &&
            access("/run/selyra/status.sock",
                   F_OK) == 0) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready)
        fail("System services unavailable");

#ifdef SELYRA_STORAGE_READ_ONLY
    const int readonly = 1;
#else
    const int readonly = 0;
#endif

    struct statfs fs;
    struct statvfs info;

    if (statfs("/data", &fs) < 0 ||
        fs.f_type != EXT4_SUPER_MAGIC ||
        statvfs("/data", &info) < 0 ||
        (!!(info.f_flag & ST_RDONLY) != readonly))
        fail("Unexpected storage access mode");

#ifdef SELYRA_STORAGE_WRITE

    int fd = open(FILE_PATH,
                  O_WRONLY | O_CREAT | O_EXCL |
                  O_NOFOLLOW, 0600);

    if (fd < 0)
        fail("Cannot create persistence file");

    if (write(fd, payload, sizeof(payload) - 1)
        != (ssize_t)(sizeof(payload) - 1))
        fail("Cannot write persistent data");

    if (fsync(fd) < 0 || close(fd) < 0)
        fail("Cannot synchronize file");

    int dir = open("/data", O_RDONLY | O_DIRECTORY);

    if (dir < 0 || fsync(dir) < 0 || close(dir) < 0)
        fail("Cannot synchronize directory");

    puts("PASS: Persistent file written and synchronized");

#else

    char buffer[sizeof(payload)];

    int fd = open(FILE_PATH, O_RDONLY | O_NOFOLLOW);

    if (fd < 0)
        fail("Persistent file missing");

    ssize_t count = read(fd, buffer, sizeof(buffer));
    close(fd);

    if (count != (ssize_t)(sizeof(payload) - 1) ||
        memcmp(buffer, payload, sizeof(payload) - 1) != 0)
        fail("Persistent data mismatch");

    puts("PASS: Persistent file survived shutdown");
    puts("PASS: Persistent file contents verified");

#ifdef SELYRA_STORAGE_READ_ONLY
    int write_fd = open(FILE_PATH, O_WRONLY | O_NOFOLLOW);

    if (write_fd >= 0) {
        close(write_fd);
        fail("Read-only storage accepted write access");
    }

    if (errno != EROFS)
        fail("Unexpected read-only write error");

    puts("PASS: Root write access rejected in read-only mode");
#endif

#endif

    test_storage_security();

    pid_t reporter = fork();

    if (reporter < 0)
        fail("Cannot fork storage reporter");

    if (reporter == 0) {
        execl("/bin/statusctl", "statusctl",
              "storage", (char *)NULL);
        _exit(1);
    }

    int report_status;

    if (waitpid(reporter, &report_status, 0) < 0 ||
        !WIFEXITED(report_status) ||
        WEXITSTATUS(report_status) != 0)
        fail("Storage report failed");

    fflush(stdout);

    execl("/bin/powerctl", "powerctl",
          "poweroff", (char *)NULL);

    fail("Cannot execute powerctl");
}

int main(void)
{
    if (getpid() != 1 || geteuid() != 0)
        return 1;

    setvbuf(stdout, NULL, _IONBF, 0);

    pid_t child = fork();

    if (child < 0)
        fail("Cannot fork test helper");

    if (child == 0) {
        probe();
        _exit(1);
    }

    execl("/selyra-init", "init", (char *)NULL);

    fail("Cannot start Selyra init");
    return 1;
}
