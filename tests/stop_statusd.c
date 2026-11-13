#include <stdio.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getuid() != 1100 || getpid() != 2) {
        puts("statusd: invalid test identity");
        return 45;
    }

    puts("statusd: clean-exit test");
    return 0;
}
