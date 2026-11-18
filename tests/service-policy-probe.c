#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PHASE
#error PHASE required
#endif

static void fail(const char *message)
{
    dprintf(2, "POLICY-FAIL: %s\n", message);
    _exit(1);
}

static int ctl(const char *action, const char *name)
{
    pid_t child = fork();

    if (child < 0)
        return -1;

    if (child == 0) {
        execl("/bin/servicectl", "servicectl",
              action, name, (char *)NULL);
        _exit(127);
    }

    int status;

    if (waitpid(child, &status, 0) != child ||
        !WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}

static int check_state(int enabled)
{
    FILE *fp = fopen("/run/selyra-state/services", "r");

    if (!fp)
        return 0;

    char line[256];
    int logd = 0, statusd = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (enabled == -1)
            logd |= !strncmp(line, "logd=stopped ", 13);
        else if (enabled)
            logd |= !strncmp(line, "logd=launched ", 14);
        else
            logd |= !strncmp(line, "logd=disabled ", 14);

        statusd |= !strncmp(line, "statusd=launched ", 17);
    }

    fclose(fp);

    int socket_ready =
        access("/run/selyra-logs/log.sock", F_OK) == 0;

    return logd && statusd &&
        socket_ready == (enabled == 1) &&
        access("/run/selyra/status.sock", F_OK) == 0;
}

static void probe(void)
{
    int enabled = PHASE != 2;
    int ready = 0;

    for (int i = 0; i < 300; ++i) {
        if (check_state(enabled)) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready)
        fail("Unexpected initial service state");

    if (PHASE == 1) {
        if (ctl("disable", "logd") != 0)
            fail("Cannot disable automatic startup");

        if (ctl("disable", "statusd") != 1)
            fail("All services could be disabled");

        puts("PASS: At least one service remains enabled");

    } else if (PHASE == 2) {
        puts("PASS: Disabled service policy restored");

        if (ctl("start", "logd") != 0)
            fail("Manual startup rejected");

        for (int i = 0; i < 150 && !check_state(1); ++i)
            usleep(100000);

        if (!check_state(1))
            fail("Manual startup failed");

        puts("PASS: Disabled service started manually");

        FILE *policy = fopen("/data/services.conf", "r");

        if (!policy)
            fail("Cannot read boot policy");

        char line[128];
        int disabled = 0;

        while (fgets(line, sizeof(line), policy))
            if (!strcmp(line, "logd.enabled=0\n"))
                disabled = 1;

        fclose(policy);

        if (!disabled)
            fail("Manual startup changed boot policy");

        puts("PASS: Automatic startup policy unchanged");

        if (ctl("stop", "logd") != 0)
            fail("Manual stop rejected");

        for (int i = 0; i < 150 && !check_state(-1); ++i)
            usleep(100000);

        if (!check_state(-1))
            fail("Manual stop failed");

        enabled = -1;

        puts("PASS: Manually started service stopped");

        if (ctl("enable", "logd") != 0)
            fail("Cannot enable automatic startup");

    } else {
        puts("PASS: Enabled service policy restored");

        if (ctl("disable", "logd") != 1)
            fail("Read-only policy accepted update");

        puts("PASS: Read-only policy update rejected");
    }

    sleep(2);

    if (!check_state(enabled))
        fail("Policy changed current service state");

    puts("PASS: Current service state preserved");
    printf("PASS: Policy phase %d completed\n", PHASE);
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
        fail("Cannot fork probe");

    if (child == 0) {
        probe();
        _exit(1);
    }

    execl("/selyra-init", "init", (char *)NULL);

    fail("Cannot execute init");
    return 1;
}
