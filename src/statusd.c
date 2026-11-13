#include <stdio.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    puts("statusd: started");

    for (;;) {
        printf("statusd: alive (PID %ld)\n",
               (long)getpid());

        sleep(5);
    }
}
