#include <stdio.h>
#include "../src/shutdown_state.h"

#define REQUIRE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", \
                #expr, __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    struct selyra_shutdown_state s = {0};

    REQUIRE(selyra_shutdown_restart_allowed(&s));

    REQUIRE(selyra_shutdown_begin(
        &s, SELYRA_POWER_NONE, 100) == -1);

    REQUIRE(s.phase == SELYRA_SHUTDOWN_RUNNING);

    REQUIRE(selyra_shutdown_begin(
        &s, SELYRA_POWER_REBOOT, 100) == 0);

    REQUIRE(s.phase == SELYRA_SHUTDOWN_GRACEFUL);
    REQUIRE(s.action == SELYRA_POWER_REBOOT);

    REQUIRE(!selyra_shutdown_restart_allowed(&s));

    REQUIRE(selyra_shutdown_begin(
        &s, SELYRA_POWER_OFF, 101) == -1);

    REQUIRE(selyra_shutdown_check_timeout(
        &s, 104, 5) == 0);

    REQUIRE(selyra_shutdown_check_timeout(
        &s, 105, 5) == 1);

    REQUIRE(s.phase == SELYRA_SHUTDOWN_FORCE);

    REQUIRE(selyra_shutdown_check_timeout(
        &s, 106, 5) == -1);

    REQUIRE(selyra_shutdown_finish(&s) == 0);
    REQUIRE(s.phase == SELYRA_SHUTDOWN_READY);

    REQUIRE(!selyra_shutdown_restart_allowed(&s));
    REQUIRE(selyra_shutdown_finish(&s) == -1);

    puts("PASS: shutdown state transitions");
    puts("PASS: service restarts disabled during shutdown");
    puts("PASS: repeated requests and invalid transitions rejected");

    return 0;
}
