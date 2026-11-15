#ifndef SELYRA_SYSTEM_INFO_H
#define SELYRA_SYSTEM_INFO_H

#include <stdio.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#ifndef SELYRA_VERSION
#error "SELYRA_VERSION must be defined by the build"
#endif

static int selyra_system_format(const struct utsname *uts,
                                long uptime,
                                char *out, size_t cap)
{
    if (!uts || !out || cap == 0 || uptime < 0)
        return -1;

    int n = snprintf(out, cap,
        "system: os=Selyra OS version=%s kernel=%s arch=%s uptime=%lds\n",
        SELYRA_VERSION, uts->release, uts->machine, uptime);

    return (n >= 0 && (size_t)n < cap) ? 0 : -1;
}

static int selyra_system_report(char *out, size_t cap)
{
    struct utsname uts;
    struct sysinfo info;

    if (uname(&uts) != 0 || sysinfo(&info) != 0)
        return -1;

    return selyra_system_format(&uts, info.uptime, out, cap);
}

#endif
