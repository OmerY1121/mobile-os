#define _GNU_SOURCE
#include <dirent.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void report(const char *message)
{
    dprintf(STDOUT_FILENO, "MO-E2E: %s\n", message);
}

static void fail(const char *message)
{
    report(message);
    _exit(1);
}

static int ctl(const char *action, const char *name,
               int unprivileged)
{
    pid_t child = fork();

    if (child < 0)
        return -1;

    if (child == 0) {
        if (unprivileged &&
            (setgroups(0, NULL) < 0 ||
             setgid(1000) < 0 || setuid(1000) < 0))
            _exit(125);

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

static pid_t find_service(const char *name)
{
    DIR *dir = opendir("/proc");

    if (!dir)
        return -2;

    pid_t found = -1;
    struct dirent *entry;

    while ((entry = readdir(dir))) {
        char *end;
        long pid = strtol(entry->d_name, &end, 10);

        if (*end || pid <= 1)
            continue;

        char path[80], comm[64];

        snprintf(path, sizeof(path),
                 "/proc/%ld/comm", pid);

        FILE *file = fopen(path, "r");

        if (!file)
            continue;

        int ok = fgets(comm, sizeof(comm), file) != NULL;
        fclose(file);

        if (!ok)
            continue;

        comm[strcspn(comm, "\n")] = '\0';

        if (!strcmp(comm, name)) {
            if (found > 1) {
                found = -2;
                break;
            }

            found = (pid_t)pid;
        }
    }

    closedir(dir);
    return found;
}

static pid_t wait_service(const char *name,
                          int running, pid_t previous)
{
    for (int i = 0; i < 150; ++i) {
        pid_t pid = find_service(name);

        if ((!running && pid == -1) ||
            (running && pid > 1 && pid != previous))
            return pid;

        usleep(100000);
    }

    fail("FAIL service transition timeout");
    return -2;
}

static int service_state_ready(void)
{
    FILE *fp = fopen("/run/selyra-state/services", "r");

    if (!fp)
        return 0;

    char line[256];
    int logd = 0, statusd = 0, configd = 0;

    while (fgets(line, sizeof(line), fp)) {
        logd |= !strncmp(line, "logd=launched ", 14);
        statusd |= !strncmp(line, "statusd=launched ", 17);
        configd |= !strncmp(line, "configd=launched ", 17);
    }

    fclose(fp);

    return logd && statusd && configd;
}

static void probe(void)
{
    for (int i = 0; i < 300 && !service_state_ready(); ++i)
        usleep(100000);

    if (!service_state_ready())
        fail("FAIL initial service state unavailable");

    report("PASS initial service state ready");

    const char *names[] = {"logd", "statusd"};

    for (int i = 0; i < 2; ++i) {
        const char *name = names[i];

        wait_service(name, 1, 0);

        if (ctl("status", name, 0) != 0)
            fail("FAIL service status");

        pid_t original = find_service(name);

        if (original <= 1)
            fail("FAIL original service PID");

        if (ctl("stop", name, 1) != 1 ||
            find_service(name) != original)
            fail("FAIL authorization");

        report("PASS unauthorized command rejected");

        if (ctl("stop", name, 0) != 0)
            fail("FAIL stop request");

        wait_service(name, 0, 0);

        sleep(2);

        if (find_service(name) != -1)
            fail("FAIL unwanted automatic restart");

        report("PASS service stopped");

        if (ctl("start", name, 0) != 0)
            fail("FAIL start request");

        pid_t started =
            wait_service(name, 1, original);

        report("PASS service started");

        if (ctl("restart", name, 0) != 0)
            fail("FAIL restart request");

        wait_service(name, 1, started);

        report("PASS service restarted with new PID");

        if (ctl("start", name, 0) != 1)
            fail("FAIL duplicate start accepted");

        report("PASS duplicate start rejected");

        report(i == 0 ?
            "PASS logd management" :
            "PASS statusd management");
    }

    report("COMPLETE");
    _exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;

    if (getpid() != 1 || geteuid() != 0)
        return 1;

    pid_t child = fork();

    if (child < 0)
        fail("FAIL probe fork");

    if (child == 0)
        probe();

    execv("/init-real", argv);
    fail("FAIL original init");
    return 1;
}
