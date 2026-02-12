#ifndef PING_H
#define PING_H

#include "soundshare.h"

/**
 * Start the ping/latency server (called by the streamer).
 * Listens on PING_PORT, responds to PING_REQUEST with PING_RESPONSE,
 * reads LATENCY_REPORT updates.
 */
int  ping_server_start(void);
void ping_server_stop(void);

/**
 * Start the ping client (called by the receiver).
 * Connects to server_ip:PING_PORT, measures RTT,
 * reports smoothed latency back to the server.
 */
int  ping_client_start(const char *server_ip);
void ping_client_stop(void);

#endif /* PING_H */