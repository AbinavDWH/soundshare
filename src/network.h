#ifndef NETWORK_H
#define NETWORK_H

/*
 * Feature macros — set here so every file
 * that includes network.h gets them
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "soundshare.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fcntl.h>
#include <poll.h>

int  net_get_device_ip(char *buf, size_t len);
int  net_create_server(int port, int backlog);
int  net_accept_client(int server_fd, char *client_ip, size_t ip_len);
int  net_connect(const char *host, int port, int timeout_ms);
void net_set_audio_opts(int fd, int send_buf_size);
void net_close(int *fd);
int  net_set_nonblocking(int fd, bool nonblock);
int  net_poll_read(int fd, int timeout_ms);
int  net_poll_write(int fd, int timeout_ms);

#endif /* NETWORK_H */