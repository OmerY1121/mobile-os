#!/usr/bin/env python3
import os
import selectors
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path.cwd()
KERNEL = ROOT / "build/vmlinuz"
ROOTFS = ROOT / "build/rootfs"

GUEST = r'''
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void report(const char *message)
{
    int fd = open("/dev/console",
                  O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        fd = STDOUT_FILENO;
    dprintf(fd, "MO-E2E: %s\n", message);
    if (fd != STDOUT_FILENO)
        close(fd);
}

static int run_ctl(
    const char *command, char *output, size_t capacity)
{
    int pipefd[2];
    if (capacity < 2 || pipe(pipefd) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);
        close(pipefd[1]);
        execl("/bin/statusctl", "statusctl",
              command, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    size_t used = 0;
    int failed = 0;

    while (used < capacity - 1) {
        struct pollfd pfd = {
            .fd = pipefd[0],
            .events = POLLIN
        };

        int rc;
        do {
            rc = poll(&pfd, 1, 3000);
        } while (rc < 0 && errno == EINTR);

        if (rc <= 0) {
            failed = 1;
            break;
        }

        ssize_t n = read(
            pipefd[0], output + used,
            capacity - used - 1
        );

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            failed = 1;
            break;
        }
        if (n == 0)
            break;
        used += (size_t)n;
    }

    output[used] = '\0';
    close(pipefd[0]);

    if (failed)
        kill(pid, SIGKILL);

    int status;
    if (waitpid(pid, &status, 0) != pid ||
        failed || !WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}

static pid_t find_logd(void)
{
    DIR *dir = opendir("/proc");
    if (!dir)
        return -1;

    pid_t found = -1;
    struct dirent *entry;

    while ((entry = readdir(dir))) {
        char *end = NULL;
        long pid = strtol(entry->d_name, &end, 10);

        if (!entry->d_name[0] ||
            *end || pid <= 1)
            continue;

        char path[80];
        if (snprintf(path, sizeof(path),
                     "/proc/%ld/comm", pid) >=
            (int)sizeof(path))
            continue;

        FILE *file = fopen(path, "r");
        if (!file)
            continue;

        char name[64] = {0};
        int valid = fgets(name, sizeof(name), file) != NULL;
        fclose(file);

        if (valid && strcmp(name, "logd\n") == 0) {
            found = (pid_t)pid;
            break;
        }
    }

    closedir(dir);
    return found;
}

static void probe(void)
{
    char reply[256];
    int ready = 0;

    for (int attempt = 0; attempt < 200; ++attempt) {
        int rc = run_ctl(
            "services", reply, sizeof(reply)
        );

        if (rc == 0 &&
            strstr(reply, "logd=launched") &&
            strstr(reply, "statusd=launched") &&
            strstr(reply, "last_exit=none")) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready) {
        report("FAIL services IPC");
        _exit(1);
    }

    report("PASS services IPC from guest");

    ready = 0;

    for (int attempt = 0; attempt < 100; ++attempt) {
        int rc = run_ctl(
            "health", reply, sizeof(reply)
        );

        if (rc == 0 &&
            strcmp(reply,
                "health: statusd=ok logd=ok\n") == 0) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready) {
        report("FAIL health IPC");
        _exit(1);
    }

    report("PASS health from guest");

    pid_t logd = find_logd();

    if (logd <= 1 || kill(logd, SIGKILL) != 0) {
        report("FAIL cannot inject logd crash");
        _exit(1);
    }

    report("PASS injected logd SIGKILL");

    ready = 0;

    for (int attempt = 0; attempt < 100; ++attempt) {
        int rc = run_ctl(
            "services", reply, sizeof(reply)
        );

        char *entry = strstr(
            reply, "logd=launched failures="
        );
        char *last = entry
            ? strstr(entry, "last_exit=signal-9")
            : NULL;
        char *end = entry ? strchr(entry, ';') : NULL;

        if (rc == 0 && last &&
            (!end || last < end)) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready) {
        report("FAIL logd restart or last_exit");
        _exit(1);
    }

    report("PASS logd restarted with signal-9");

    ready = 0;

    for (int attempt = 0; attempt < 50; ++attempt) {
        int rc = run_ctl(
            "health", reply, sizeof(reply)
        );

        if (rc == 0 &&
            strcmp(reply,
                "health: statusd=ok logd=ok\n") == 0) {
            ready = 1;
            break;
        }

        usleep(100000);
    }

    if (!ready) {
        report("FAIL health after restart");
        _exit(1);
    }

    report("PASS health recovered after crash");
    report("COMPLETE");
    _exit(0);
}

int main(int argc, char **argv)
{
    (void)argc;

    pid_t child = fork();
    if (child < 0) {
        report("FAIL cannot start probe");
        return 1;
    }

    if (child == 0)
        probe();

    execv("/init-real", argv);
    report("FAIL cannot start original init");
    return 1;
}
'''

if not KERNEL.is_file() or not (ROOTFS / "init").is_file():
    raise SystemExit("Run make first.")

expected = [
    "MO-E2E: PASS services IPC from guest",
    "MO-E2E: PASS health from guest",
    "MO-E2E: PASS injected logd SIGKILL",
    "MO-E2E: PASS logd restarted with signal-9",
    "MO-E2E: PASS health recovered after crash",
    "MO-E2E: COMPLETE",
]

with tempfile.TemporaryDirectory(
    prefix="mo-e2e-", dir=ROOT / "build"
) as tmp:
    work = Path(tmp)
    guest_root = work / "rootfs"

    shutil.copytree(ROOTFS, guest_root, symlinks=True)

    source = work / "probe.c"
    binary = guest_root / "init"
    original = guest_root / "init-real"
    source.write_text(GUEST)

    binary.rename(original)

    subprocess.run([
        "gcc", "-static", "-O2",
        "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        str(source), "-o", str(binary)
    ], check=True)

    image = work / "initramfs.gz"

    pack = r'''
set -euo pipefail
cd "$1"
mkdir -p dev
rm -f dev/console dev/null
mknod -m 600 dev/console c 5 1
mknod -m 666 dev/null c 1 3
chown 0:0 etc/selyra/services.conf
chmod 0600 etc/selyra/services.conf
find . -print0 |
    cpio --null --quiet -o -H newc |
    gzip -9 > "$2"
'''

    subprocess.run([
        "fakeroot", "bash", "-c", pack,
        "bash", str(guest_root), str(image)
    ], check=True)

    command = [
        "qemu-system-x86_64",
        "-m", "512",
        "-accel", "tcg",
        "-nographic",
        "-nic", "none",
        "-kernel", str(KERNEL),
        "-initrd", str(image),
        "-append", "console=ttyS0 rdinit=/init",
        "-no-reboot"
    ]

    print("Starting QEMU end-to-end test...", flush=True)

    proc = subprocess.Popen(
        command,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )

    output = ""
    selector = selectors.DefaultSelector()
    deadline = time.monotonic() + 70

    try:
        selector.register(proc.stdout, selectors.EVENT_READ)

        while time.monotonic() < deadline:
            for key, _ in selector.select(0.25):
                chunk = os.read(key.fileobj.fileno(), 4096)

                if not chunk:
                    selector.unregister(key.fileobj)
                    continue

                output += chunk.decode(
                    "utf-8", errors="replace"
                )
                output = output[-200000:]

            if ("MO-E2E: COMPLETE" in output or
                "MO-E2E: FAIL" in output):
                break

            if proc.poll() is not None:
                break
    finally:
        selector.close()

        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)

        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()

    missing = [item for item in expected
               if item not in output]

    for item in expected:
        if item in output:
            print(item)

    if missing or "MO-E2E: FAIL" in output:
        print("FAIL: QEMU end-to-end integration")
        print("Missing checks:", missing)
        print("\nLast guest output:")
        print(output[-6000:])
        raise SystemExit(1)

print("All QEMU end-to-end checks passed.")
