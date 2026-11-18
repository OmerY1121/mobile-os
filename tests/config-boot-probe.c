#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PHASE
#error PHASE required
#endif

static void fail(const char *message)
{
    dprintf(2, "CONFIG-FAIL: %s\n", message);
    _exit(1);
}

static int command(const char *tool, const char *action,
                   const char *key, const char *value,
                   int unprivileged, char *out, size_t cap)
{
    int fds[2];

    if (pipe(fds) < 0)
        fail("pipe");

    pid_t child = fork();

    if (child < 0)
        fail("fork");

    if (!child) {
        close(fds[0]);

        if (dup2(fds[1], STDOUT_FILENO) < 0)
            _exit(125);

        close(fds[1]);

        if (unprivileged && (setgroups(0, NULL) < 0 ||
            setgid(1000) < 0 || setuid(1000) < 0))
            _exit(125);

        char *args[] = {
            (char *)tool, (char *)action,
            (char *)key, (char *)value, NULL
        };

        execv(tool, args);
        _exit(127);
    }

    close(fds[1]);

    size_t used = 0;

    while (used < cap - 1) {
        ssize_t n = read(fds[0], out + used, cap - 1 - used);

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0)
            fail("read command output");

        if (!n)
            break;

        used += (size_t)n;
    }

    if (used == cap - 1)
        fail("command response too large");

    out[used] = '\0';
    close(fds[0]);

    int status;
    pid_t result;

    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);

    if (result != child || !WIFEXITED(status))
        fail("command wait");

    return WEXITSTATUS(status);
}

static void expect(const char *action, const char *key,
                   const char *value, int code,
                   const char *output, int unprivileged)
{
    char out[256];

    int rc = command("/bin/configctl", action, key, value,
                     unprivileged, out, sizeof(out));

    if (rc != code || strcmp(out, output))
        fail("unexpected configctl result");
}

static void hostname_is(const char *expected)
{
    char name[256];

    if (gethostname(name, sizeof(name)) < 0 ||
        strcmp(name, expected))
        fail("kernel hostname mismatch");
}

static void verify_rotation(void)
{
    char message[401], out[256];

    memset(message, 'x', sizeof(message)-1);
    message[sizeof(message)-1] = '\0';

    for (int i = 0; i < 50; ++i)
        if (command("/bin/logctl", "write", message, NULL,
                    0, out, sizeof(out)) ||
            strcmp(out, "OK\n"))
            fail("log write failed");

    struct stat current, rotated;

    if (stat("/run/selyra-logdata/current.log", &current) < 0 ||
        stat("/run/selyra-logdata/current.log.1", &rotated) < 0 ||
        !S_ISREG(current.st_mode) ||
        !S_ISREG(rotated.st_mode) ||
        current.st_size <= 0 ||
        current.st_size > 16 * 1024 ||
        rotated.st_size < 15 * 1024 ||
        rotated.st_size > 16 * 1024)
        fail("16 KiB log rotation not applied");

    puts("PASS: 16 KiB log rotation verified");
}

static void probe(void)
{
    int ready = 0;

    for (int i = 0; i < 300; ++i) {
        if (!access("/run/selyra-config/config.sock", F_OK) &&
            !access("/run/selyra-logs/log.sock", F_OK) &&
            !access("/run/selyra-power/control.sock", F_OK)) {

            char out[256];

            if (!command("/bin/configctl", "get", "system.hostname",
                         NULL, 0, out, sizeof(out))) {
                ready = 1;
                break;
            }
        }

        usleep(100000);
    }

    if (!ready)
        fail("services unavailable");

    if (PHASE == 1) {
        hostname_is("selyra");

        expect("get", "logging.max_kib", NULL, 0, "64\n", 0);
        expect("set", "system.hostname", "selyra-config-test", 0, "OK\n", 0);
        expect("set", "logging.max_kib", "16", 0, "OK\n", 0);
        expect("get", "system.hostname", NULL, 0, "selyra-config-test\n", 0);

        hostname_is("selyra");

        puts("PASS: Settings saved for next boot");

    } else {
        hostname_is("selyra-config-test");

        expect("get", "system.hostname", NULL, 0, "selyra-config-test\n", 0);
        expect("get", "logging.max_kib", NULL, 0, "16\n", 0);

        puts("PASS: Saved settings restored and kernel hostname verified");

        verify_rotation();

        if (PHASE == 3) {
            expect("set", "logging.max_kib", "64", 1, "", 0);
            expect("reset", "system.hostname", NULL, 1, "", 0);

            puts("PASS: Read-only settings protected");
        }
    }

    expect("set", "logging.max_kib", "128", 1, "", 1);
    expect("get", "logging.max_kib", NULL, 0, "16\n", 0);

    puts("PASS: Unauthorized settings update rejected");

    printf("PASS: Configuration phase %d complete\n", PHASE);
    fflush(stdout);

    execl("/bin/powerctl", "powerctl", "poweroff", (char *)NULL);

    fail("powerctl exec");
}

int main(void)
{
    if (getpid() != 1 || geteuid() != 0)
        return 1;

    setvbuf(stdout, NULL, _IONBF, 0);

    pid_t child = fork();

    if (child < 0)
        fail("probe fork");

    if (!child)
        probe();

    execl("/selyra-init", "init", (char *)NULL);

    fail("init exec");
    return 1;
}
