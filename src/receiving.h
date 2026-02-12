#ifndef RECEIVING_H
#define RECEIVING_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "soundshare.h"
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Receiving context */
typedef struct {
    AudioConfig cfg;
    int         socket_fd;
    char        server_ip[INET_ADDRSTRLEN];
    pthread_t   receive_thread;
    bool        running;
} ReceiveContext;

int  receiving_start(const char *server_ip);
void receiving_stop(void);

#endif /* RECEIVING_H */
