#include <stdio.h>
#include <string.h>
#include "../src/cpu_info.h"

int main(void)
{
    struct selyra_cpu_sample a, b;
    unsigned int usage = 0;
    char report[128];

    if (selyra_cpu_parse(
            "cpu 10 0 10 80 0 0 0 0 0 0\n", &a) != 0 ||
        selyra_cpu_parse(
            "cpu 20 0 20 160 0 0 0 0 0 0\n", &b) != 0 ||
        selyra_cpu_usage(&a, &b, &usage) != 0 ||
        usage != 200 ||
        selyra_cpu_parse("cpu0 1 2 3 4\n", &a) == 0 ||
        selyra_cpu_parse("cpu 1 2 3\n", &a) == 0 ||
        selyra_cpu_parse("cpu -1 0 0 0\n", &a) == 0 ||
        selyra_cpu_parse(
            "cpu 18446744073709551615 1 0 0\n",
            &a) == 0 ||
        selyra_cpu_report(report, sizeof(report)) != 0 ||
        strncmp(report, "cpu: cores=", 11) != 0) {
        fputs("FAIL: CPU parser or live sample\n", stderr);
        return 1;
    }

    puts("PASS: CPU parser, usage calculation, and live /proc/stat");
    puts(report);
    return 0;
}
