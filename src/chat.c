#include "chat.h"
#include "protocol.h"
#include "network.h"
#include "ui.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

#define MAX_CHAT_CLIENTS 16

/* ---- Callback ---- */
static ChatMessageCallback g_chat_cb      = NULL;
static void               *g_chat_cb_data = NULL;

void chat_set_callback(ChatMessageCallback cb, void *user_data)
{
    g_chat_cb      = cb;
    g_chat_cb_data = user_data;
}

static void notify_message(const char *sender, const char *msg)
{
    if (g_chat_cb) g_chat_cb(sender, msg, g_chat_cb_data);
}

/* ---- Wire format helpers ---- */

static int chat_write_msg(int fd, const char *sender, const char *message)
{
    uint16_t slen = (uint16_t)strlen(sender);
    uint16_t mlen = (uint16_t)strlen(message);

    uint8_t hdr[5];
    hdr[0] = CHAT_MSG;
    write_be16(hdr + 1, slen);

    if (write_fully(fd, hdr, 3) < 0) return -1;
    if (write_fully(fd, sender, slen) < 0) return -1;

    uint8_t mhdr[2];
    write_be16(mhdr, mlen);
    if (write_fully(fd, mhdr, 2) < 0) return -1;
    if (write_fully(fd, message, mlen) < 0) return -1;

    return 0;
}

static int chat_read_msg(int fd, char *sender, size_t smax,
                         char *message, size_t mmax)
{
    uint8_t buf2[2];

    if (read_fully(fd, buf2, 2) != 2) return -1;
    uint16_t slen = read_be16(buf2);
    if (slen >= smax) return -1;
    if (read_fully(fd, sender, slen) != slen) return -1;
    sender[slen] = '\0';

    if (read_fully(fd, buf2, 2) != 2) return -1;
    uint16_t mlen = read_be16(buf2);
    if (mlen >= mmax) return -1;
    if (read_fully(fd, message, mlen) != mlen) return -1;
    message[mlen] = '\0';

    return 0;
}

/* ============================================================ */
/*  CHAT SERVER                                                  */
/* ============================================================ */

static struct {
    int        server_fd;
    int        clients[MAX_CHAT_CLIENTS];
    atomic_bool client_connected[MAX_CHAT_CLIENTS];
    int        client_count;
    pthread_t  thread;
    pthread_t  client_threads[MAX_CHAT_CLIENTS];
    bool       running;
    pthread_mutex_t lock;
} csrv;

static void chat_srv_broadcast_except(int exclude_idx,
                                      const char *sender, const char *msg)
{
    pthread_mutex_lock(&csrv.lock);
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (i == exclude_idx) continue;
        if (!atomic_load(&csrv.client_connected[i])) continue;
        chat_write_msg(csrv.clients[i], sender, msg);  /* best-effort */
    }
    pthread_mutex_unlock(&csrv.lock);
}

typedef struct { int idx; } ChatClientArg;

static void *chat_client_handler(void *arg)
{
    ChatClientArg *a = (ChatClientArg *)arg;
    int idx = a->idx;
    int fd  = csrv.clients[idx];
    free(a);

    char sender[CHAT_MAX_SENDER];
    char message[CHAT_MAX_MSG];

    while (atomic_load(&g_app.is_streaming) &&
           atomic_load(&csrv.client_connected[idx]))
    {
        int ready = net_poll_read(fd, 1000);
        if (ready <= 0) {
            if (ready < 0) break;
            continue;
        }

        uint8_t cmd;
        if (read(fd, &cmd, 1) != 1) break;

        if (cmd == CHAT_MSG) {
            if (chat_read_msg(fd, sender, sizeof(sender),
                              message, sizeof(message)) < 0)
                break;

            notify_message(sender, message);
            chat_srv_broadcast_except(idx, sender, message);
        }
    }

    pthread_mutex_lock(&csrv.lock);
    net_close(&csrv.clients[idx]);
    atomic_store(&csrv.client_connected[idx], false);
    csrv.client_count--;
    pthread_mutex_unlock(&csrv.lock);

    return NULL;
}

static void *chat_server_thread(void *arg)
{
    (void)arg;
    LOG_I("Chat server started on port %d", CHAT_PORT);

    while (atomic_load(&g_app.is_streaming)) {
        int ready = net_poll_read(csrv.server_fd, 1000);
        if (ready <= 0) continue;

        char ip[INET_ADDRSTRLEN];
        int fd = net_accept_client(csrv.server_fd, ip, sizeof(ip));
        if (fd < 0) continue;

        /* Find a slot */
        int idx = -1;
        pthread_mutex_lock(&csrv.lock);
        for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
            if (!atomic_load(&csrv.client_connected[i])) {
                idx = i;
                csrv.clients[i] = fd;
                atomic_store(&csrv.client_connected[i], true);
                csrv.client_count++;
                break;
            }
        }
        pthread_mutex_unlock(&csrv.lock);

        if (idx < 0) {
            LOG_W("Chat: max clients reached, rejecting %s", ip);
            close(fd);
            continue;
        }

        LOG_I("Chat client connected: %s (slot %d)", ip, idx);
        notify_message("", ip);  /* system message via UI */

        ChatClientArg *a = malloc(sizeof(*a));
        a->idx = idx;
        pthread_create(&csrv.client_threads[idx], NULL, chat_client_handler, a);
        pthread_detach(csrv.client_threads[idx]);
    }

    LOG_I("Chat server stopped");
    return NULL;
}

int chat_server_start(void)
{
    memset(&csrv, 0, sizeof(csrv));
    csrv.server_fd = -1;
    pthread_mutex_init(&csrv.lock, NULL);

    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        csrv.clients[i] = -1;
        atomic_store(&csrv.client_connected[i], false);
    }

    csrv.server_fd = net_create_server(CHAT_PORT, 8);
    if (csrv.server_fd < 0) return -1;

    csrv.running = true;
    if (pthread_create(&csrv.thread, NULL, chat_server_thread, NULL) != 0) {
        net_close(&csrv.server_fd);
        return -1;
    }
    return 0;
}

void chat_server_stop(void)
{
    net_close(&csrv.server_fd);

    pthread_mutex_lock(&csrv.lock);
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (atomic_load(&csrv.client_connected[i])) {
            net_close(&csrv.clients[i]);
            atomic_store(&csrv.client_connected[i], false);
        }
    }
    pthread_mutex_unlock(&csrv.lock);

    if (csrv.running) {
        pthread_join(csrv.thread, NULL);
        csrv.running = false;
    }
    pthread_mutex_destroy(&csrv.lock);
}

void chat_server_broadcast(const char *sender, const char *message)
{
    pthread_mutex_lock(&csrv.lock);
    for (int i = 0; i < MAX_CHAT_CLIENTS; i++) {
        if (atomic_load(&csrv.client_connected[i]))
            chat_write_msg(csrv.clients[i], sender, message);
    }
    pthread_mutex_unlock(&csrv.lock);
}

/* ============================================================ */
/*  CHAT CLIENT                                                  */
/* ============================================================ */

static struct {
    int       fd;
    pthread_t thread;
    bool      running;
    char      server_ip[INET_ADDRSTRLEN];
    pthread_mutex_t write_lock;
} ccli = { .fd = -1 };

static void *chat_client_thread(void *arg)
{
    (void)arg;

    usleep(500000);

    int fd = net_connect(ccli.server_ip, CHAT_PORT, 5000);
    if (fd < 0) {
        LOG_W("Chat client: cannot connect");
        return NULL;
    }

    pthread_mutex_lock(&ccli.write_lock);
    ccli.fd = fd;
    pthread_mutex_unlock(&ccli.write_lock);

    notify_message("", "Connected to chat");

    char sender[CHAT_MAX_SENDER];
    char message[CHAT_MAX_MSG];

    while (atomic_load(&g_app.is_receiving)) {
        int ready = net_poll_read(fd, 1000);
        if (ready <= 0) {
            if (ready < 0) break;
            continue;
        }

        uint8_t cmd;
        if (read(fd, &cmd, 1) != 1) break;

        if (cmd == CHAT_MSG) {
            if (chat_read_msg(fd, sender, sizeof(sender),
                              message, sizeof(message)) < 0)
                break;
            notify_message(sender, message);
        }
    }

    pthread_mutex_lock(&ccli.write_lock);
    net_close(&ccli.fd);
    pthread_mutex_unlock(&ccli.write_lock);

    notify_message("", "Chat disconnected");
    LOG_I("Chat client stopped");
    return NULL;
}

int chat_client_start(const char *server_ip)
{
    ccli.fd = -1;
    strncpy(ccli.server_ip, server_ip, INET_ADDRSTRLEN - 1);
    pthread_mutex_init(&ccli.write_lock, NULL);

    ccli.running = true;
    if (pthread_create(&ccli.thread, NULL, chat_client_thread, NULL) != 0) {
        LOG_E("pthread_create(chat_client): %s", strerror(errno));
        return -1;
    }
    return 0;
}

void chat_client_stop(void)
{
    pthread_mutex_lock(&ccli.write_lock);
    net_close(&ccli.fd);
    pthread_mutex_unlock(&ccli.write_lock);

    if (ccli.running) {
        pthread_join(ccli.thread, NULL);
        ccli.running = false;
    }
    pthread_mutex_destroy(&ccli.write_lock);
}

void chat_client_send(const char *sender, const char *message)
{
    pthread_mutex_lock(&ccli.write_lock);
    if (ccli.fd >= 0) {
        chat_write_msg(ccli.fd, sender, message);
    }
    pthread_mutex_unlock(&ccli.write_lock);
}