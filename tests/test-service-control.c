#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../src/service_control.h"

int main(void)
{
    struct selyra_service_request req;

    const char *verbs[] = {
        "START", "STOP", "RESTART", "ENABLE", "DISABLE"
    };

    const char *names[] = {
        "logd", "statusd"
    };

    char input[64];

    for (unsigned a = 0; a < 5; ++a)
        for (unsigned s = 0; s < 2; ++s) {
            int n = snprintf(input, sizeof(input),
                             "%s %s\n", verbs[a], names[s]);

            assert(selyra_service_parse(
                input, (size_t)n, 0, &req) == 0);

            assert(req.action ==
                (enum selyra_service_action)(a + 1));

            assert(req.target ==
                (enum selyra_service_target)s);
        }

    puts("PASS: Service actions parsed");

    const char *bad[] = {
        "START configd\n",
        "START 1\n",
        "STOP ../logd\n",
        "STOP logd",
        "STOP  logd\n",
        "STOP logd\nSTART logd\n",
        "REBOOT\n",
        "START logd \n"
    };

    for (unsigned i = 0;
         i < sizeof(bad)/sizeof(bad[0]); ++i) {

        assert(selyra_service_parse(
            bad[i], strlen(bad[i]), 0, &req) == -1);

        assert(req.action == SERVICE_NONE);
    }

    const char binary[] = "STOP logd\0\n";

    assert(selyra_service_parse(
        binary, sizeof(binary)-1, 0, &req) == -1);

    puts("PASS: Invalid and protected service requests rejected");

    for (unsigned i = 1000; i <= 1102; ++i) {
        assert(selyra_service_parse(
            "STOP logd\n", 10, i, &req) == -2);

        assert(req.action == SERVICE_NONE);
    }

    puts("PASS: Non-root requests denied");

    int pair[2];

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET,
                      0, pair) == 0);

    assert(send(pair[0], "STOP logd\n", 10, 0) == 10);

    if (geteuid() == 0) {
        assert(selyra_service_receive(pair[1], &req) == 0);
        assert(req.action == SERVICE_STOP);
        assert(req.target == SERVICE_LOGD);

        assert(selyra_service_receive(pair[1], &req) == -1);

        memset(input, 'A', sizeof(input));

        assert(send(pair[0], input,
                    sizeof(input), 0) == sizeof(input));

        assert(selyra_service_receive(pair[1], &req) == -1);

        puts("PASS: Root IPC, timeout and oversized packet rejection");

    } else {
        assert(selyra_service_receive(pair[1], &req) == -2);

        puts("PASS: Real non-root IPC peer denied");
    }

    close(pair[0]);
    close(pair[1]);

    puts("All Selyra service control tests passed.");

    return 0;
}
