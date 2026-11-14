#ifndef SELYRA_POWER_SOCKET_H
#define SELYRA_POWER_SOCKET_H

#include "power_control.h"

int selyra_power_socket_open(const char *path);

/* 1: valid request, 0: none/rejected, -1: error */
int selyra_power_socket_poll(
    int server,
    enum selyra_power_action *action);

#endif
