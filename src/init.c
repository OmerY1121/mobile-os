#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <grp.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STATUSD_UID 1100
#define STATUSD_GID 1100

#define MAX_QUICK_FAILURES 3
#define STABLE_SECONDS 30

static int prepare_statusd_directory(void)
{
    if (chown("/run/mobile-os",
              STATUSD_UID, STATUSD_GID) < 0) {
        perror("init: chown status directory");
        return -1;
    }

    if (chmod("/run/mobile-os", 0700) < 0) {
        perror("init: chmod status directory");
        return -1;
    }

    return 0;
}


static int setup_private_directory(
    const char *path, uid_t uid, gid_t gid)
{
    if (mkdir(path, 0700) < 0 && errno != EEXIST) {
        perror("init: mkdir");
        return -1;
    }

    if (chown(path, uid, gid) < 0) {
        perror("init: chown");
        return -1;
    }

    if (chmod(path, 0700) < 0) {
        perror("init: chmod");
        return -1;
    }

    return 0;
}

static int prepare_private_storage(void)
{
    if (chown("/var/lib/mobile-os", 0, 0) < 0 ||
        chmod("/var/lib/mobile-os", 0755) < 0) {
        perror("init: storage parent");
        return -1;
    }

    if (setup_private_directory(
            "/var/lib/mobile-os/system", 0, 0) < 0)
        return -1;

    if (setup_private_directory(
            "/var/lib/mobile-os/statusd",
            STATUSD_UID, STATUSD_GID) < 0)
        return -1;

    puts("init: private directories ready");
    return 0;
}


static int isolate_statusd_mounts(void)
{
    if (unshare(CLONE_NEWNS) < 0) {
        perror("init: unshare");
        return -1;
    }

    if (mount(NULL, "/", NULL,
              MS_REC | MS_PRIVATE, NULL) < 0) {
        perror("init: mount propagation");
        return -1;
    }

    if (mount("tmpfs", "/tmp", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC,
              "size=4m,mode=0700,uid=1100,gid=1100") < 0) {
        perror("init: private tmpfs");
        return -1;
    }

    puts("init: statusd private tmpfs ready");
    return 0;
}

static int wait_for_service(pid_t child)
{
    int status;

    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR)
            continue;

        perror("init: namespace waitpid");
        return 125;
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);

    return 125;
}


static void cleanup_namespace_children(void)
{
    int status;
    int reaped = 0;
    pid_t pid;

    puts("init: namespace cleanup started");

    if (kill(-1, SIGTERM) < 0 && errno != ESRCH)
        perror("init: namespace SIGTERM");

    for (int attempt = 0; attempt < 20; attempt++) {
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            reaped++;

        if (pid < 0 && errno == ECHILD) {
            printf("init: namespace cleanup reaped %d orphan(s)\n",
                   reaped);
            return;
        }

        if (pid < 0 && errno != EINTR) {
            perror("init: namespace reap");
            break;
        }

        usleep(100000);
    }

    puts("init: namespace cleanup escalating to SIGKILL");

    if (kill(-1, SIGKILL) < 0 && errno != ESRCH)
        perror("init: namespace SIGKILL");

    for (;;) {
        pid = waitpid(-1, &status, 0);

        if (pid > 0) {
            reaped++;
            continue;
        }

        if (pid < 0 && errno == EINTR)
            continue;

        if (pid < 0 && errno != ECHILD)
            perror("init: namespace final reap");

        break;
    }

    printf("init: namespace cleanup reaped %d orphan(s)\n",
           reaped);
}

static void run_namespace_init(void)
{
    if (getpid() != 1) {
        fputs("init: namespace init must be PID 1\n",
              stderr);
        _exit(126);
    }

    if (mount("proc", "/proc", "proc",
              MS_NOSUID | MS_NODEV | MS_NOEXEC,
              "hidepid=2") < 0) {
        perror("init: namespace /proc");
        _exit(126);
    }

    puts("init: statusd restricted /proc ready");
    puts("init: namespace supervisor running as PID 1");

    pid_t service = fork();

    if (service < 0) {
        perror("init: namespace fork");
        _exit(126);
    }

    if (service == 0) {
        if (setgroups(0, NULL) < 0 ||
            setgid(STATUSD_GID) < 0 ||
            setuid(STATUSD_UID) < 0) {
            perror("init: drop service privileges");
            _exit(126);
        }

        printf("init: statusd UID=%ld GID=%ld namespace PID=%ld PPID=%ld\n",
               (long)getuid(), (long)getgid(),
               (long)getpid(), (long)getppid());

        if (prctl(PR_SET_NO_NEW_PRIVS, 1L,
                  0L, 0L, 0L) < 0) {
            perror("init: no_new_privs");
            _exit(126);
        }

        execl("/bin/statusd", "statusd", (char *)NULL);
        perror("init: exec statusd");
        _exit(127);
    }

    int code = wait_for_service(service);

    printf("init: namespace statusd exited with code %d\n",
           code);

    cleanup_namespace_children();
    _exit(code);
}

static void launch_isolated_statusd(void)
{
    if (isolate_statusd_mounts() < 0)
        _exit(126);

    if (unshare(CLONE_NEWPID) < 0) {
        perror("init: PID namespace");
        _exit(126);
    }

    pid_t ns_init = fork();

    if (ns_init < 0) {
        perror("init: fork namespace init");
        _exit(126);
    }

    if (ns_init == 0)
        run_namespace_init();

    _exit(wait_for_service(ns_init));
}

static void manage_statusd(void)
{
    int quick_failures = 0;

    for (;;) {
        time_t started = time(NULL);
        pid_t child = fork();

        if (child < 0) {
            perror("init: fork");
            sleep(2);
            continue;
        }

        if (child == 0)
            launch_isolated_statusd();

        printf("init: statusd launcher started (PID %ld)\n",
               (long)child);

        int status;

        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR)
                continue;

            perror("init: waitpid");
            return;
        }

        if (WIFEXITED(status)) {
            printf("init: statusd exited with code %d\n",
                   WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("init: statusd stopped by signal %d\n",
                   WTERMSIG(status));
        }

        if (WIFEXITED(status) &&
            WEXITSTATUS(status) == 0) {
            puts("init: statusd stopped normally");
            break;
        }

        if (time(NULL) - started >= STABLE_SECONDS)
            quick_failures = 0;

        if (++quick_failures >= MAX_QUICK_FAILURES) {
            puts("init: statusd restart limit reached");
            break;
        }

        sleep(quick_failures);
    }
}

int main(void)
{
    if (getpid() != 1) {
        fputs("init must run as PID 1\n", stderr);
        return 1;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    puts("Mobile OS 0.0.7-dev");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        perror("init: mount /proc");

    if (prepare_private_storage() == 0 &&
        prepare_statusd_directory() == 0)
        manage_statusd();
    else
        fputs("init: statusd not started\n", stderr);

    for (;;)
        pause();
}
