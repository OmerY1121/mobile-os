#include <stdio.h>
#include <string.h>
#include "../src/system_info.h"

int main(void)
{
    struct utsname uts = {0};
    char out[256];
    char small[8];
    char expected[256];

    snprintf(uts.release, sizeof(uts.release), "6.12.0-test");
    snprintf(uts.machine, sizeof(uts.machine), "x86_64");

    snprintf(expected, sizeof(expected),
        "system: os=Selyra OS version=%s kernel=6.12.0-test arch=x86_64 uptime=42s\n",
        SELYRA_VERSION);

    if (selyra_system_format(&uts, 42, out, sizeof(out)) != 0 ||
        strcmp(out, expected) != 0 ||
        selyra_system_format(&uts, 42, small, sizeof(small)) == 0 ||
        selyra_system_format(&uts, -1, out, sizeof(out)) == 0 ||
        selyra_system_report(out, sizeof(out)) != 0 ||
        strncmp(out, "system: os=Selyra OS version=", 29) != 0) {
        fputs("FAIL: system info\n", stderr);
        return 1;
    }

    puts("PASS: system info formatting and live kernel data");
    return 0;
}
