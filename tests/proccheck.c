#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("proccheck: UID=%ld PID=%ld\n",
           (long)getuid(), (long)getpid());

    if (getuid() != 1100) {
        puts("FAIL: wrong service identity");
        return 1;
    }

    FILE *file = fopen("/proc/self/status", "r");

    if (!file) {
        perror("FAIL: own process inaccessible");
        return 1;
    }

    fclose(file);
    puts("PASS: own process accessible");

    errno = 0;
    file = fopen("/proc/1/status", "r");

    if (file) {
        fclose(file);
        puts("FAIL: root process accessible");
        return 1;
    }

    if (errno != ENOENT && errno != EACCES) {
        perror("FAIL: unexpected error");
        return 1;
    }

    puts("PASS: root process hidden");
    puts("All process isolation tests passed.");

    for (;;)
        pause();
}
