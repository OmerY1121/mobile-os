#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;

    if (sigemptyset(&sa.sa_mask) < 0 ||
        sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("stubborn-logd: sigaction");
        return 1;
    }

    puts("SEL-POWER-E2E: stubborn logd running");
    fflush(stdout);

    for (;;)
        pause();
}
