#include "../src/service_state.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;

    struct mo_service_state services[] = {
        {"logd", "launched", 0, 0, 0},
        {"statusd", "launched", 0, 0, 0}
    };

    char buffer[1024];

    if (mo_state_write(argv[1], services, 2) != 0)
        return 1;

    if (mo_state_read(
            argv[1], buffer, sizeof(buffer)) != 0)
        return 1;

    if (!strstr(buffer, "logd=launched failures=0") ||
        !strstr(buffer, "statusd=launched failures=0"))
        return 1;

    puts("PASS: service state writing");
    puts("PASS: service state reading");

    services[1].state = "retrying";
    services[1].failures = 2;
    services[1].exit_kind = 1;
    services[1].exit_value = 7;

    if (mo_state_write(argv[1], services, 2) != 0)
        return 1;

    if (mo_state_read(
            argv[1], buffer, sizeof(buffer)) != 0)
        return 1;

    if (!strstr(buffer, "statusd=retrying failures=2"))
        return 1;

    if (!strstr(buffer, "last_exit=exit-7"))
        return 1;

    puts("PASS: last exit code recorded");
    puts("PASS: service state updates");

    services[1].state = "invalid state";

    if (mo_state_write(argv[1], services, 2) == 0)
        return 1;

    if (mo_state_read(
            argv[1], buffer, sizeof(buffer)) != 0)
        return 1;

    if (!strstr(buffer, "statusd=retrying failures=2"))
        return 1;

    puts("PASS: invalid state rejected");
    puts("PASS: previous state preserved");

    puts("All service state tests passed.");
    return 0;
}
