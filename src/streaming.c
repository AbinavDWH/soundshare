#include "streaming.h"
#include "protocol.h"
#include "network.h"
#include "audio.h"
#include "ping.h"
#include "chat.h"
#include "ui.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

static StreamContext ctx;

static void add_client(int fd, const char *ip)
{
    pthread_mutex_lock(&ctx.clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!atomic_load(&ctx.clients[i].connected)) {
            ctx.clients[i].fd = fd;
            snprintf(ctx.clients[i].ip, INET_ADDRSTRLEN, "%s", ip);
            atomic_store(&ctx.clients[i].connected, true);
            ctx.client_count++;
            atomic_store(&g_app.receiver_count, ctx.client_count);
            LOG_I("Client connected: %s (total %d)", ip, ctx.client_count);
            break;
        }
    }
    pthread_mutex_unlock(&ctx.clients_lock);
    ui_update_receiver_count(ctx.client_count);
}

static void remove_client(int idx)
{
    pthread_mutex_lock(&ctx.clients_lock);
    if (atomic_load(&ctx.clients[idx].connected)) {
        LOG_I("Client disconnected: %s", ctx.clients[idx].ip);
        net_close(&ctx.clients[idx].fd);
        atomic_store(&ctx.clients[idx].connected, false);
        ctx.client_count--;
        if (ctx.client_count < 0) ctx.client_count = 0;
        atomic_store(&g_app.receiver_count, ctx.client_count);
    }
    pthread_mutex_unlock(&ctx.clients_lock);
    ui_update_receiver_count(ctx.client_count);
}

static void *accept_thread_func(void *arg)
{
    (void)arg;
    LOG_I("Accept thread started");

    while (atomic_load(&g_app.is_streaming)) {
        int ready = net_poll_read(ctx.server_fd, 1000);
        if (ready <= 0) continue;

        char client_ip[INET_ADDRSTRLEN] = {0};
        int client_fd = net_accept_client(ctx.server_fd, client_ip, sizeof(client_ip));
        if (client_fd < 0) continue;

        net_set_audio_opts(client_fd, ctx.config.socket_buffer_size);

        if (protocol_write_header(client_fd, &ctx.config) < 0) {
            LOG_W("Failed to send header to %s", client_ip);
            close(client_fd);
            continue;
        }

        add_client(client_fd, client_ip);

        char status[128];
        snprintf(status, sizeof(status), "Streaming to %d receiver(s)",
                 atomic_load(&g_app.receiver_count));
        ui_update_status(status);
    }

    LOG_I("Accept thread stopped");
    return NULL;
}

static void *stream_thread_func(void *arg)
{
    (void)arg;
    LOG_I("Stream thread started");

    AudioCapture *cap = audio_capture_open(&ctx.config);
    if (!cap) {
        LOG_E("Failed to open audio capture");
        ui_update_status("Audio capture failed");
        atomic_store(&g_app.is_streaming, false);
        return NULL;
    }

    uint8_t *pcm_buf = malloc((size_t)ctx.config.chunk_size);
    if (!pcm_buf) {
        LOG_E("malloc failed");
        audio_capture_close(cap);
        atomic_store(&g_app.is_streaming, false);
        return NULL;
    }

    atomic_store(&g_app.stream_start_time, current_time_ms());
    atomic_store(&g_app.last_time_ms, current_time_ms());
    atomic_store(&g_app.bytes_sent_this_second, 0);
    atomic_store(&g_app.total_bytes_sent, 0);

    while (atomic_load(&g_app.is_streaming)) {
        int rd = audio_capture_read(cap, pcm_buf, (size_t)ctx.config.chunk_size);
        if (rd <= 0) {
            if (atomic_load(&g_app.is_streaming))
                LOG_W("Capture read error");
            break;
        }

        int active = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!atomic_load(&ctx.clients[i].connected)) continue;
            active++;

            ssize_t w = write_fully(ctx.clients[i].fd, pcm_buf, (size_t)rd);
            if (w < 0) {
                remove_client(i);
            }
        }

        if (active == 0) continue;

        int64_t bytes = (int64_t)rd * active;
        atomic_fetch_add(&g_app.bytes_sent_this_second, bytes);
        atomic_fetch_add(&g_app.total_bytes_sent, bytes);

        int64_t now  = current_time_ms();
        int64_t diff = now - atomic_load(&g_app.last_time_ms);
        if (diff >= 1000) {
            int64_t b = atomic_exchange(&g_app.bytes_sent_this_second, 0);
            int64_t kbps = (b * 8) / diff;
            atomic_store(&g_app.last_time_ms, now);
            ui_update_stats(kbps,
                            atomic_load(&g_app.total_bytes_sent),
                            now - atomic_load(&g_app.stream_start_time));
        }
    }

    free(pcm_buf);
    audio_capture_close(cap);

    LOG_I("Stream thread stopped");
    return NULL;
}

int streaming_start(int preset_index)
{
    memset(&ctx, 0, sizeof(ctx));
    pthread_mutex_init(&ctx.clients_lock, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        ctx.clients[i].fd = -1;
        atomic_store(&ctx.clients[i].connected, false);
    }

    config_load_preset(&ctx.config, preset_index);

    ctx.server_fd = net_create_server(AUDIO_PORT, 8);
    if (ctx.server_fd < 0) {
        ui_update_status("Failed to bind audio port");
        return -1;
    }

    atomic_store(&g_app.is_streaming, true);
    atomic_store(&g_app.receiver_count, 0);
    atomic_store(&g_app.current_latency_ms, -1);

    char fmt[256], sr[64];
    config_format_string(&ctx.config, fmt, sizeof(fmt));
    config_sample_rate_string(&ctx.config, sr, sizeof(sr));
    ui_show_streaming(fmt);
    ui_update_format_info(sr, fmt);

    char status[256], ip[64];
    net_get_device_ip(ip, sizeof(ip));
    snprintf(status, sizeof(status),
             "Streaming on %s:%d - waiting for receivers...", ip, AUDIO_PORT);
    ui_update_status(status);

    ping_server_start();
    chat_server_start();

    ctx.accept_running = true;
    ctx.stream_running = true;

    if (pthread_create(&ctx.accept_thread, NULL, accept_thread_func, NULL) != 0) {
        LOG_E("pthread_create(accept): %s", strerror(errno));
        streaming_stop();
        return -1;
    }

    if (pthread_create(&ctx.stream_thread, NULL, stream_thread_func, NULL) != 0) {
        LOG_E("pthread_create(stream): %s", strerror(errno));
        streaming_stop();
        return -1;
    }

    return 0;
}

void streaming_stop(void)
{
    if (!atomic_exchange(&g_app.is_streaming, false))
        return;

    LOG_I("Stopping streaming...");
    ui_update_status("Stopping...");

    ping_server_stop();
    chat_server_stop();
    net_close(&ctx.server_fd);

    pthread_mutex_lock(&ctx.clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (atomic_load(&ctx.clients[i].connected)) {
            net_close(&ctx.clients[i].fd);
            atomic_store(&ctx.clients[i].connected, false);
        }
    }
    ctx.client_count = 0;
    pthread_mutex_unlock(&ctx.clients_lock);

    if (ctx.accept_running) {
        pthread_join(ctx.accept_thread, NULL);
        ctx.accept_running = false;
    }
    if (ctx.stream_running) {
        pthread_join(ctx.stream_thread, NULL);
        ctx.stream_running = false;
    }

    pthread_mutex_destroy(&ctx.clients_lock);
    atomic_store(&g_app.receiver_count, 0);

    ui_reset();
    ui_update_status("Streaming stopped");
}

int streaming_client_count(void)
{
    return atomic_load(&g_app.receiver_count);
}