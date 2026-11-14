
#include <stdio.h>
#include <unistd.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname system;

    if (uname(&system) != 0) {
        perror("uname");
        return 1;
    }

    printf("Mobile OS - Development Build 0.0.1\n");
    printf("Kernel: %s\n", system.release);
    printf("Process ID: %ld\n", (long)getpid());

    return 0;
}
