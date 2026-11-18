#ifndef SELYRA_SERVICE_CONTROL_H
#define SELYRA_SERVICE_CONTROL_H

#include <stddef.h>
#include <sys/types.h>

enum selyra_service_action {
    SERVICE_NONE,
    SERVICE_START,
    SERVICE_STOP,
    SERVICE_RESTART,
    SERVICE_ENABLE,
    SERVICE_DISABLE
};

enum selyra_service_target {
    SERVICE_LOGD,
    SERVICE_STATUSD
};

struct selyra_service_request {
    enum selyra_service_action action;
    enum selyra_service_target target;
};

/* 0: valid, -1: invalid, -2: denied */
int selyra_service_parse(const char *data, size_t length,
                         uid_t uid,
                         struct selyra_service_request *request);

int selyra_service_receive(int fd,
                           struct selyra_service_request *request);

#endif
