#include <stdio.h>
#include <string.h>
#include "../src/memory_info.h"

static int check(const char *sample, int expect_success)
{
    FILE *f = tmpfile();
    if (!f) return 0;

    if (fputs(sample, f) == EOF ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    char out[128];
    int result = selyra_memory_from_stream(
        f, out, sizeof(out));
    fclose(f);

    if (expect_success)
        return result == 0 &&
            strcmp(out,
                "memory: total=1000KiB available=600KiB used=400KiB\n"
            ) == 0;

    return result != 0;
}

int main(void)
{
    char live[128];

    if (!check("MemTotal: 1000 kB\nMemAvailable: 600 kB\n", 1) ||
        !check("MemTotal: 1000 kB\n", 0) ||
        !check("MemTotal: 1000 kB\nMemAvailable: 1100 kB\n", 0) ||
        !check("MemTotal: nope kB\nMemAvailable: 600 kB\n", 0) ||
        !check("MemTotal: 1000 kB\nMemTotal: 1000 kB\nMemAvailable: 600 kB\n", 0) ||
        selyra_memory_report(live, sizeof(live)) != 0) {
        fputs("FAIL: memory parser\n", stderr);
        return 1;
    }

    puts("PASS: memory parser and live /proc/meminfo");
    return 0;
}
