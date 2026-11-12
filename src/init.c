
#include <stdio.h>
#include <unistd.h>
#include <sys/mount.h>

int main(void)
{
    // Only run as the first userspace process.
    if (getpid() != 1) {
        fprintf(stderr, "Error: init must run as PID 1.\n");
        return 1;
    }

    // Disable output buffering for early boot.
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Mobile OS 0.0.1\n");
    printf("Initializing system...\n");

    // Mount the Linux process information filesystem.
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("Failed to mount /proc");
    }

    printf("Initialization reached idle state.\n");

    // Keep PID 1 alive until we implement services.
    for (;;) {
        pause();
    }
}
