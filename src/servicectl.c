#define _GNU_SOURCE
#include "service_query.h"
#include "service_client.h"
#include <stdio.h>
#include <stdlib.h>

static int valid_name(const char *name)
{
    size_t n = strlen(name);

    return n > 0 && n <= 32 &&
        strspn(name,
            "abcdefghijklmnopqrstuvwxyz0123456789-_") == n;
}

int main(int argc, char **argv)
{
    if (argc == 3 && valid_name(argv[2])) {
        const char *action = NULL;

        if (!strcmp(argv[1], "start"))
            action = "START";
        else if (!strcmp(argv[1], "stop"))
            action = "STOP";
        else if (!strcmp(argv[1], "restart"))
            action = "RESTART";
        else if (!strcmp(argv[1], "enable"))
            action = "ENABLE";
        else if (!strcmp(argv[1], "disable"))
            action = "DISABLE";

        if (action)
            return service_command(action, argv[2]);
    }

    int list = argc == 2 &&
        !strcmp(argv[1], "list");

    int status = argc == 3 &&
        !strcmp(argv[1], "status") &&
        valid_name(argv[2]);

    if (!list && !status) {
        fputs("usage: servicectl list | status NAME | start NAME | stop NAME | restart NAME | enable NAME | disable NAME\n",
              stderr);
        return 2;
    }

    const char *path = getenv("SELYRA_SERVICE_STATE");

    if (!path || !*path)
        path = "/run/selyra-state/services";

    char report[1024];

    if (mo_query_services(path, report,
                          sizeof(report)) < 0) {
        fputs("servicectl: service state unavailable\n",
              stderr);
        return 1;
    }

    size_t length = strlen(report);
    report[length - 1] = '\0';

    char *save = NULL;
    char *entries[16];
    size_t count = 0;
    char *selected = NULL;

    for (char *line = strtok_r(report + 10, ";", &save);
         line; line = strtok_r(NULL, ";", &save)) {

        if (*line == ' ')
            ++line;

        char *equal = strchr(line, '=');

        if (!equal || count == 16)
            goto invalid;

        *equal = '\0';

        if (!valid_name(line))
            goto invalid;

        if (!list && !strcmp(line, argv[2])) {
            if (selected)
                goto invalid;

            selected = line;
        }

        *equal = '=';
        entries[count++] = line;
    }

    if (!count)
        goto invalid;

    if (list) {
        for (size_t i = 0; i < count; ++i)
            puts(entries[i]);

    } else if (selected) {
        puts(selected);

    } else {
        fputs("servicectl: unknown service\n", stderr);
        return 1;
    }

    return 0;

invalid:
    fputs("servicectl: invalid service state\n", stderr);
    return 1;
}
