#include "ping.h"
#include "protocol.h"
#include "network.h"
#include "config.h"
#include "ui.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

/* ============================================================ */
/*  PING SERVER (streamer side)                                  */
/* ============================================================ */

static struct {
    int       server_fd;
    pthread_t thread;
    bool      running;
} ping_srv = { .server_fd = -1 };

static void handle_ping_client(int fd)
{
    uint8_t cmd;

    while (atomic_load(&g_app.is_streaming)) {
        int ready = net_poll_read(fd, 1000);
        if (ready <= 0) {
            if (ready < 0) break;
            continue;
        }

        ssize_t n = read(fd, &cmd, 1);
        if (n <= 0) break;

        if (cmd == PING_REQUEST) {
            uint8_t resp = PING_RESPONSE;
            if (write_fully(fd, &resp, 1) < 0) break;
        } else if (cmd == LATENCY_REPORT) {
            uint8_t buf[8];
            if (read_fully(fd, buf, 8) != 8) break;
            int64_t ms = 0;
            for (int i = 0; i < 8; i++)
                ms = (ms << 8) | buf[i];
            atomic_store(&g_app.current_latency_ms, ms);
            ui_update_latency(ms);
        }
    }
}

static void *ping_server_thread(void *arg)
{
    (void)arg;
    LOG_I("Ping server started on port %d", PING_PORT);

    while (atomic_load(&g_app.is_streaming)) {
        int ready = net_poll_read(ping_srv.server_fd, 1000);
        if (ready <= 0) continue;

        char ip[INET_ADDRSTRLEN];
        int fd = net_accept_client(ping_srv.server_fd, ip, sizeof(ip));
        if (fd < 0) continue;

        LOG_I("Ping client connected: %s", ip);
        handle_ping_client(fd);
        net_close(&fd);
        LOG_I("Ping client disconnected: %s", ip);
    }

    LOG_I("Ping server stopped");
    return NULL;
}

int ping_server_start(void)
{
    ping_srv.server_fd = net_create_server(PING_PORT, 4);
    if (ping_srv.server_fd < 0) return -1;

    ping_srv.running = true;
    if (pthread_create(&ping_srv.thread, NULL, ping_server_thread, NULL) != 0) {
        LOG_E("pthread_create(ping_server): %s", strerror(errno));
        net_close(&ping_srv.server_fd);
        return -1;
    }

    return 0;
}

void ping_server_stop(void)
{
    net_close(&ping_srv.server_fd);
    if (ping_srv.running) {
        pthread_join(ping_srv.thread, NULL);
        ping_srv.running = false;
    }
}

/* ============================================================ */
/*  PING CLIENT (receiver side)                                  */
/* ============================================================ */

static struct {
    int       fd;
    pthread_t thread;
    bool      running;
    char      server_ip[INET_ADDRSTRLEN];
} ping_cli = { .fd = -1 };

static void *ping_client_thread(void *arg)
{
    (void)arg;
    LOG_I("Ping client starting – target %s:%d", ping_cli.server_ip, PING_PORT);

    /* Small delay so the server is ready */
    usleep(500000);

    int fd = net_connect(ping_cli.server_ip, PING_PORT, 3000);
    if (fd < 0) {
        LOG_W("Ping: could not connect");
        return NULL;
    }
    ping_cli.fd = fd;

    int64_t smoothed = -1;

    while (atomic_load(&g_app.is_receiving)) {
        /* Send PING_REQUEST */
        int64_t start_ns = current_time_ns();
        uint8_t req = PING_REQUEST;
        if (write_fully(fd, &req, 1) < 0) break;

        /* Wait for response */
        int ready = net_poll_read(fd, 2000);
        if (ready <= 0) {
            atomic_store(&g_app.current_latency_ms, 999);
            ui_update_latency(999);
            if (ready < 0) break;
            usleep(500000);
            continue;
        }

        uint8_t resp;
        if (read(fd, &resp, 1) != 1) break;

        if (resp == PING_RESPONSE) {
            int64_t rtt_ms = (current_time_ns() - start_ns) / 1000000;

            /* Add estimated buffer latency */
            AudioConfig tmp;
            config_load_preset(&tmp, g_app.selected_preset);
            int64_t buf_ms = (int64_t)config_buffer_latency_ms(&tmp);
            int64_t total  = rtt_ms / 2 + buf_ms;

            if (smoothed < 0)
                smoothed = total;
            else
                smoothed = (smoothed * 7 + total * 3) / 10;

            atomic_store(&g_app.current_latency_ms, smoothed);
            ui_update_latency(smoothed);

            /* Report latency back to server */
            uint8_t report[9];
            report[0] = LATENCY_REPORT;
            for (int i = 0; i < 8; i++)
                report[1 + i] = (uint8_t)(smoothed >> (56 - i * 8));

            write_fully(fd, report, sizeof(report));  /* best-effort */
        }

        usleep(500000);  /* 500 ms between pings */
    }

    net_close(&fd);
    ping_cli.fd = -1;

    LOG_I("Ping client stopped");
    return NULL;
}

int ping_client_start(const char *server_ip)
{
    strncpy(ping_cli.server_ip, server_ip, INET_ADDRSTRLEN - 1);
    ping_cli.running = true;

    if (pthread_create(&ping_cli.thread, NULL, ping_client_thread, NULL) != 0) {
        LOG_E("pthread_create(ping_client): %s", strerror(errno));
        return -1;
    }
    return 0;
}

void ping_client_stop(void)
{
    net_close(&ping_cli.fd);
    if (ping_cli.running) {
        pthread_join(ping_cli.thread, NULL);
        ping_cli.running = false;
    }
}