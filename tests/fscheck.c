#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("fscheck: UID=%ld GID=%ld\n",
           (long)getuid(), (long)getgid());

    if (getuid() != 1100 || getgid() != 1100) {
        puts("FAIL: incorrect service identity");
        return 1;
    }

    FILE *file = fopen(
        "/var/lib/mobile-os/statusd/test.txt", "w");

    if (!file) {
        perror("FAIL: own directory");
        return 1;
    }

    fputs("filesystem test\n", file);
    fclose(file);

    puts("PASS: own directory writable");

    errno = 0;
    file = fopen(
        "/var/lib/mobile-os/system/test.txt", "w");

    if (file) {
        fclose(file);
        puts("FAIL: system directory writable");
        return 1;
    }

    if (errno != EACCES) {
        perror("FAIL: unexpected filesystem error");
        return 1;
    }

    puts("PASS: system directory access denied");
    puts("All filesystem tests passed.");

    for (;;)
        pause();
}
