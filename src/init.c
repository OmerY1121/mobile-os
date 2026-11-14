#define _GNU_SOURCE
#include "log_client.h"
#include "service_config.h"
#include "service_state.h"
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <grp.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STATUSD_UID 1100
#define STATUSD_GID 1100
#define LOGD_UID 1101
#define LOGD_GID 1101

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



static int prepare_logd_directory(void)
{
    if (setup_private_directory(
            "/run/mobile-os-logs",
            LOGD_UID, STATUSD_GID) < 0)
        return -1;

    if (chmod("/run/mobile-os-logs", 0710) < 0) {
        perror("init: log socket directory");
        return -1;
    }

    if (setup_private_directory(
            "/run/mobile-os-logdata",
            LOGD_UID, LOGD_GID) < 0)
        return -1;

    puts("init: private log directories ready");
    return 0;
}


static int prepare_state_directory(void)
{
    const char *path = "/run/mobile-os-state";

    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        perror("init: mkdir state");
        return -1;
    }

    struct stat st;

    if (lstat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
        fputs("init: invalid state directory\n", stderr);
        return -1;
    }

    if (chown(path, 0, 0) < 0 ||
        chmod(path, 0755) < 0) {
        perror("init: state directory permissions");
        return -1;
    }

    puts("init: service state directory ready");
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







struct managed_service {
    const char *name;
    const char *start_label;
    void (*launch)(void);
    pid_t pid;
    time_t started;
    time_t retry_at;
    int failures;
    int exit_kind;
    int exit_value;
    int stopped;
    int enabled;
    int restart_limit;
};

static void launch_logd(void)
{
    if (setgroups(0, NULL) < 0 ||
        setgid(LOGD_GID) < 0 ||
        setuid(LOGD_UID) < 0) {
        perror("init: logd credentials");
        _exit(126);
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1L,
              0L, 0L, 0L) < 0) {
        perror("init: logd no_new_privs");
        _exit(126);
    }

    execl("/bin/logd", "logd", (char *)NULL);
    perror("init: exec logd");
    _exit(127);
}

static void start_service(struct managed_service *svc)
{
    pid_t child = fork();

    if (child < 0) {
        perror("init: fork service");
        svc->retry_at = time(NULL) + 2;
        return;
    }

    if (child == 0) {
        svc->launch();
        _exit(127);
    }

    svc->pid = child;
    svc->started = time(NULL);

    printf("init: %s started (PID %ld)\n",
           svc->start_label, (long)child);
}

static void service_exited(struct managed_service *svc,
                           int status)
{
    if (WIFEXITED(status)) {
        svc->exit_kind = 1;
        svc->exit_value = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        svc->exit_kind = 2;
        svc->exit_value = WTERMSIG(status);
    }

    if (svc->exit_kind != 0) {
        char event[160];

        snprintf(
            event, sizeof(event),
            "init: %s terminated %s-%d",
            svc->name,
            svc->exit_kind == 1 ? "exit" : "signal",
            svc->exit_value
        );

        puts(event);
        (void)mo_log_event(event);
    }

    svc->pid = 0;

    if (WIFEXITED(status)) {
        printf("init: %s exited with code %d\n",
               svc->name, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("init: %s stopped by signal %d\n",
               svc->name, WTERMSIG(status));
    }

    if (WIFEXITED(status) &&
        WEXITSTATUS(status) == 0) {
        printf("init: %s stopped normally\n", svc->name);
        svc->stopped = 1;
        return;
    }

    if (time(NULL) - svc->started >= STABLE_SECONDS)
        svc->failures = 0;

    if (++svc->failures >= svc->restart_limit) {
        printf("init: %s restart limit reached\n",
               svc->name);
        svc->stopped = 1;
        return;
    }

    svc->retry_at = time(NULL) + svc->failures;
}


static void publish_service_states(
    const struct managed_service *services,
    size_t count)
{
    if (count == 0 || count > 16)
        return;

    struct mo_service_state entries[16];
    char signature[1024] = {0};
    size_t used = 0;

    static char previous[1024];

    for (size_t i = 0; i < count; ++i) {
        const struct managed_service *svc = &services[i];
        const char *state;

        if (!svc->enabled)
            state = "disabled";
        else if (svc->pid > 0)
            state = "launched";
        else if (svc->stopped)
            state = svc->failures >= svc->restart_limit ?
                "failed" : "stopped";
        else
            state = "retrying";

        entries[i].name = svc->name;
        entries[i].state = state;
        entries[i].failures = svc->failures;
        entries[i].exit_kind = svc->exit_kind;
        entries[i].exit_value = svc->exit_value;

        int n = snprintf(
            signature + used, sizeof(signature) - used,
            "%s=%s:%d:%d:%d;", svc->name, state,
            svc->failures, svc->exit_kind, svc->exit_value
        );

        if (n < 0 ||
            (size_t)n >= sizeof(signature) - used)
            return;

        used += (size_t)n;
    }

    if (strcmp(previous, signature) == 0)
        return;

    if (mo_state_write(
            "/run/mobile-os-state/services",
            entries, count) < 0) {
        perror("init: publish service state");
        return;
    }

    strcpy(previous, signature);
    puts("init: service state published");
}

static void manage_services(
    const struct mo_service_config *config)
{
    struct managed_service services[] = {
        {
            .name = "logd",
            .start_label = "logd",
            .launch = launch_logd,
            .enabled = config->logd_enabled,
            .restart_limit = config->logd_restart_limit
        },
        {
            .name = "statusd",
            .start_label = "statusd launcher",
            .launch = launch_isolated_statusd,
            .enabled = config->statusd_enabled,
            .restart_limit = config->statusd_restart_limit
        }
    };

    const size_t count =
        sizeof(services) / sizeof(services[0]);

    if (services[0].enabled) {
        start_service(&services[0]);

        int logged = 0;

        for (int attempt = 0; attempt < 40; ++attempt) {
            if (mo_log_event("init: boot services starting") == 0) {
                logged = 1;
                break;
            }

            usleep(50000);
        }

        puts(logged ? "init: boot event logged"
                    : "init: boot event unavailable");
    } else {
        puts("init: logd disabled by config");
    }

    if (services[1].enabled)
        start_service(&services[1]);
    else
        puts("init: statusd disabled by config");

    publish_service_states(services, count);
    puts("init: service manager ready");

    for (;;) {
        int status;
        pid_t child;

        while ((child = waitpid(-1, &status,
                                WNOHANG)) > 0) {
            for (size_t i = 0; i < count; ++i) {
                if (services[i].pid == child) {
                    service_exited(&services[i], status);
                    break;
                }
            }
        }

        if (child < 0 &&
            errno != ECHILD &&
            errno != EINTR)
            perror("init: waitpid services");

        time_t now = time(NULL);

        for (size_t i = 0; i < count; ++i) {
            struct managed_service *svc = &services[i];

            if (svc->enabled &&
                !svc->stopped &&
                svc->pid == 0 &&
                now >= svc->retry_at)
                start_service(svc);
        }

        publish_service_states(services, count);
        sleep(1);
    }
}

int main(void)
{
    if (getpid() != 1) {
        fputs("init must run as PID 1\n", stderr);
        return 1;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    puts("Mobile OS 0.0.8");

    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        perror("init: mount /proc");

    if (prepare_private_storage() == 0 &&
        prepare_statusd_directory() == 0 &&
        prepare_logd_directory() == 0 &&
        prepare_state_directory() == 0) {
        struct mo_service_config config;

        if (mo_service_config_load(
                "/etc/mobile-os/services.conf",
                &config) == 0) {
            puts("init: service configuration loaded");
            manage_services(&config);
        } else {
            fputs("init: invalid service configuration\n",
                  stderr);
        }
    } else {
        fputs("init: service setup failed\n", stderr);
    }

    for (;;)
        pause();
}
