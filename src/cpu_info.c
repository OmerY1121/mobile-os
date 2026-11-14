#define _POSIX_C_SOURCE 200809L
#include "cpu_info.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int selyra_cpu_parse(const char *line,
                     struct selyra_cpu_sample *out)
{
    if (!line || !out ||
        strncmp(line, "cpu", 3) != 0 ||
        (line[3] != ' ' && line[3] != '\t'))
        return -1;

    const char *p = line + 3;
    unsigned long long total = 0, idle = 0;
    int count = 0;

    /* Guest times are already included in user/nice. */
    while (count < 8) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p < '0' || *p > '9') break;

        errno = 0;
        char *end;
        unsigned long long value = strtoull(p, &end, 10);

        if (errno != 0 || end == p ||
            total > ULLONG_MAX - value)
            return -1;

        total += value;

        if (count == 3 || count == 4) {
            if (idle > ULLONG_MAX - value)
                return -1;
            idle += value;
        }

        p = end;
        count++;
    }

    if (count < 4 || idle > total ||
        (count < 8 && *p != '\n' && *p != '\0'))
        return -1;

    out->total = total;
    out->idle = idle;
    return 0;
}

int selyra_cpu_usage(const struct selyra_cpu_sample *a,
                     const struct selyra_cpu_sample *b,
                     unsigned int *tenths)
{
    if (!a || !b || !tenths ||
        b->total <= a->total || b->idle < a->idle)
        return -1;

    unsigned long long total = b->total - a->total;
    unsigned long long idle = b->idle - a->idle;

    if (idle > total) return -1;

    unsigned long long busy = total - idle;
    if (busy > ULLONG_MAX / 1000) return -1;

    *tenths = (unsigned int)(busy * 1000 / total);
    return 0;
}

static int selyra_cpu_read(struct selyra_cpu_sample *out)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[512];

    int ok = fgets(line, sizeof(line), f) &&
             (strchr(line, '\n') || feof(f)) &&
             selyra_cpu_parse(line, out) == 0 &&
             !ferror(f);

    if (fclose(f) != 0) return -1;
    return ok ? 0 : -1;
}

int selyra_cpu_report(char *out, size_t cap)
{
    if (!out || cap == 0) return -1;

    struct selyra_cpu_sample a, b;
    if (selyra_cpu_read(&a) != 0) return -1;

    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 150000000L
    };

    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) return -1;
    }

    if (selyra_cpu_read(&b) != 0) return -1;

    unsigned int tenths;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);

    if (cores <= 0 ||
        selyra_cpu_usage(&a, &b, &tenths) != 0)
        return -1;

    int n = snprintf(out, cap,
        "cpu: cores=%ld usage=%u.%u%%\n",
        cores, tenths / 10, tenths % 10);

    return n >= 0 && (size_t)n < cap ? 0 : -1;
}
