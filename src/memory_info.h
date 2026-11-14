#ifndef SELYRA_MEMORY_INFO_H
#define SELYRA_MEMORY_INFO_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1: found, 0: other key, -1: invalid */
static int selyra_mem_value(const char *line, const char *key,
                            unsigned long long *value)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || line[n] != ':')
        return 0;

    const char *p = line + n + 1;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;

    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(p, &end, 10);
    if (errno != 0 || end == p) return -1;
    while (*end == ' ' || *end == '\t') end++;
    if (strncmp(end, "kB", 2) != 0) return -1;
    end += 2;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\n' && *end != '\0') return -1;

    *value = parsed;
    return 1;
}

static int selyra_memory_from_stream(FILE *stream,
                                     char *out, size_t cap)
{
    if (!stream || !out || cap == 0) return -1;

    unsigned long long total = 0, available = 0;
    int got_total = 0, got_available = 0;
    char line[256];

    while (fgets(line, sizeof(line), stream)) {
        if (!strchr(line, '\n') && !feof(stream))
            return -1;

        int t = selyra_mem_value(line, "MemTotal", &total);
        int a = selyra_mem_value(line, "MemAvailable", &available);

        if (t < 0 || a < 0 ||
            (t == 1 && got_total) ||
            (a == 1 && got_available))
            return -1;

        got_total |= (t == 1);
        got_available |= (a == 1);
    }

    if (ferror(stream) || !got_total || !got_available ||
        total == 0 || available > total)
        return -1;

    int n = snprintf(out, cap,
        "memory: total=%lluKiB available=%lluKiB used=%lluKiB\n",
        total, available, total - available);

    return (n >= 0 && (size_t)n < cap) ? 0 : -1;
}

static int selyra_memory_report(char *out, size_t cap)
{
    FILE *stream = fopen("/proc/meminfo", "r");
    if (!stream) return -1;

    int result = selyra_memory_from_stream(stream, out, cap);
    if (fclose(stream) != 0) return -1;

    return result;
}

#endif
