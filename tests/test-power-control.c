#include <stdio.h>
#include "../src/power_control.h"

int main(void)
{
    enum selyra_power_action action = SELYRA_POWER_NONE;

    if (selyra_power_parse("POWEROFF\n", 0, &action) != 0 ||
        action != SELYRA_POWER_OFF ||
        selyra_power_parse("REBOOT\n", 0, &action) != 0 ||
        action != SELYRA_POWER_REBOOT ||
        selyra_power_parse("POWEROFF\n", 1100, &action) != -2 ||
        action != SELYRA_POWER_NONE ||
        selyra_power_parse("REBOOT\n", 1101, &action) != -2 ||
        selyra_power_parse("POWEROFF", 0, &action) != -1 ||
        selyra_power_parse("REBOOT\nEXTRA\n", 0, &action) != -1 ||
        selyra_power_parse("STOP\n", 0, &action) != -1 ||
        selyra_power_parse(NULL, 0, &action) != -1 ||
        selyra_power_parse("REBOOT\n", 0, NULL) != -1) {
        fputs("FAIL: power protocol\n", stderr);
        return 1;
    }

    puts("PASS: power commands and root-only policy");
    puts("PASS: invalid and unauthorized requests rejected");
    return 0;
}
