/*
 * Feature macros — MUST be the very first lines
 * before ANY #include (even our own headers)
 */
#ifndef _GNU_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#define _POSIX_C_SOURCE 200809L

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
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "network.h"

/* ---- Fallback defines for broken toolchains ---- */
#ifndef POLLIN
#define POLLIN  0x0001
#endif

#ifndef POLLOUT
#define POLLOUT 0x0004
#endif

#ifndef POLLERR
#define POLLERR 0x0008
#endif

#ifndef POLLHUP
#define POLLHUP 0x0010
#endif

#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif

#ifndef IP_TOS
#define IP_TOS 1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

/* ------------------------------------------------------------------ */
int net_get_device_ip(char *buf, size_t len)
{
    struct ifaddrs *ifas = NULL, *ifa;

    if (getifaddrs(&ifas) == -1) {
        LOG_E("getifaddrs: %s", strerror(errno));
        snprintf(buf, len, "Not connected");
        return -1;
    }

    int found = -1;

    for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)                         continue;
        if (ifa->ifa_addr->sa_family != AF_INET)    continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)          continue;
        if (!(ifa->ifa_flags & IFF_UP))             continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)len);
        found = 0;
        break;
    }

    freeifaddrs(ifas);

    if (found != 0)
        snprintf(buf, len, "Not connected");

    return found;
}

/* ------------------------------------------------------------------ */
int net_create_server(int port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_E("socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E("bind port %d: %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        LOG_E("listen: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOG_I("Server listening on port %d", port);
    return fd;
}

/* ------------------------------------------------------------------ */
int net_accept_client(int server_fd, char *client_ip, size_t ip_len)
{
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);

    int fd = accept(server_fd, (struct sockaddr *)&addr, &alen);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            LOG_E("accept: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    if (client_ip && ip_len > 0)
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, (socklen_t)ip_len);

    return fd;
}

/* ------------------------------------------------------------------ */
int net_connect(const char *host, int port, int timeout_ms)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        LOG_E("inet_pton(%s): %s", host, strerror(errno));
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_E("socket: %s", strerror(errno));
        return -1;
    }

    net_set_nonblocking(fd, true);

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        LOG_E("connect(%s:%d): %s", host, port, strerror(errno));
        close(fd);
        return -1;
    }

    if (rc < 0) {
        int ready = net_poll_write(fd, timeout_ms);
        if (ready <= 0) {
            LOG_E("connect(%s:%d): %s", host, port,
                  ready == 0 ? "timeout" : strerror(errno));
            close(fd);
            return -1;
        }

        int err = 0;
        socklen_t elen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err) {
            LOG_E("connect(%s:%d): %s", host, port, strerror(err));
            close(fd);
            return -1;
        }
    }

    net_set_nonblocking(fd, false);

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    LOG_I("Connected to %s:%d", host, port);
    return fd;
}

/* ------------------------------------------------------------------ */
void net_set_audio_opts(int fd, int send_buf_size)
{
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    if (send_buf_size > 0)
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                   &send_buf_size, sizeof(send_buf_size));

    int tos = 0x10;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
}

/* ------------------------------------------------------------------ */
void net_close(int *fd)
{
    if (fd && *fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

/* ------------------------------------------------------------------ */
int net_set_nonblocking(int fd, bool nonblock)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;

    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return fcntl(fd, F_SETFL, flags);
}

/* ------------------------------------------------------------------ */
int net_poll_read(int fd, int timeout_ms)
{
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd     = fd;
    pfd.events = POLLIN;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0 && errno == EINTR) return 0;
    return rc;
}

int net_poll_write(int fd, int timeout_ms)
{
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd     = fd;
    pfd.events = POLLOUT;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0 && errno == EINTR) return 0;
    return rc;
}