#ifndef STREAMING_H
#define STREAMING_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "soundshare.h"
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 16

/* Per-client connection */
typedef struct {
    int         fd;
    char        ip[INET_ADDRSTRLEN];
    atomic_bool connected;
} ClientConn;

/* Streaming context */
typedef struct {
    AudioConfig     config;
    int             server_fd;
    ClientConn      clients[MAX_CLIENTS];
    int             client_count;
    pthread_mutex_t clients_lock;

    pthread_t       accept_thread;
    pthread_t       stream_thread;
    bool            accept_running;
    bool            stream_running;
} StreamContext;

int  streaming_start(int preset_index);
void streaming_stop(void);
int  streaming_client_count(void);

#endif /* STREAMING_H */
