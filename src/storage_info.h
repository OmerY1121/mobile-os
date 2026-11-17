#ifndef SELYRA_STORAGE_INFO_H
#define SELYRA_STORAGE_INFO_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

static int selyra_storage_report(char *out, size_t cap)
{
    FILE *fp = fopen("/proc/self/mountinfo", "r");

    if (!fp)
        return -1;

    char *line = NULL;
    size_t size = 0;
    int found = 0;

    while (getline(&line, &size, fp) >= 0) {
        char path[128], fs[32], device[128];

        if (sscanf(line, "%*s %*s %*s %*s %127s",
                   path) != 1 ||
            strcmp(path, "/data") != 0)
            continue;

        const char *sep = strstr(line, " - ");

        if (found || !sep ||
            sscanf(sep + 3, "%31s %127s",
                   fs, device) != 2 ||
            strcmp(fs, "ext4") != 0 ||
            strcmp(device, "/dev/vda") != 0) {
            found = -1;
            break;
        }

        found = 1;
    }

    if (ferror(fp) || (!feof(fp) && found >= 0))
        found = -1;

    free(line);
    fclose(fp);

    if (found < 0)
        return -1;

    if (!found) {
        int n = snprintf(out, cap,
                         "storage: not mounted\n");

        return n >= 0 && (size_t)n < cap ? 0 : -1;
    }

    struct statvfs st;

    if (statvfs("/data", &st) < 0 ||
        !st.f_frsize ||
        st.f_bfree > st.f_blocks ||
        st.f_bavail > st.f_bfree ||
        st.f_blocks > ULLONG_MAX / st.f_frsize)
        return -1;

    unsigned long long unit = st.f_frsize;

    unsigned long long total =
        st.f_blocks * unit / 1024;

    unsigned long long used =
        (st.f_blocks - st.f_bfree) * unit / 1024;

    unsigned long long available =
        st.f_bavail * unit / 1024;

    int n = snprintf(out, cap,
        "storage: device=/dev/vda mount=/data "
        "fs=ext4 access=%s total=%lluKiB "
        "used=%lluKiB available=%lluKiB\n",
        (st.f_flag & ST_RDONLY) ? "ro" : "rw",
        total, used, available);

    return n >= 0 && (size_t)n < cap ? 0 : -1;
}

#endif
