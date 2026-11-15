#ifndef SELYRA_CPU_INFO_H
#define SELYRA_CPU_INFO_H

#include <stddef.h>

struct selyra_cpu_sample {
    unsigned long long total;
    unsigned long long idle;
};

int selyra_cpu_parse(const char *line,
                     struct selyra_cpu_sample *out);

int selyra_cpu_usage(const struct selyra_cpu_sample *first,
                     const struct selyra_cpu_sample *second,
                     unsigned int *tenths);

int selyra_cpu_report(char *out, size_t cap);

#endif
