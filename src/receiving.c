#include "receiving.h"
#include "protocol.h"
#include "network.h"
#include "audio.h"
#include "ping.h"
#include "chat.h"
#include "ui.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

static ReceiveContext rctx;

/* ---- PCM receive loop ---- */

static int receive_pcm_loop(int fd, AudioPlayback *pb, const AudioConfig *cfg)
{
    uint8_t *buf = malloc((size_t)cfg->chunk_size);
    if (!buf) return -1;

    while (atomic_load(&g_app.is_receiving)) {
        ssize_t n = read_fully(fd, buf, (size_t)cfg->chunk_size);
        if (n <= 0) {
            if (atomic_load(&g_app.is_receiving))
                ui_update_status("Streamer disconnected");
            break;
        }

        if (audio_playback_write(pb, buf, (size_t)n) < 0)
            break;

        atomic_fetch_add(&g_app.total_bytes_sent, n);
        atomic_fetch_add(&g_app.bytes_sent_this_second, n);

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

    free(buf);
    return 0;
}

/* ---- FLAC receive loop ---- */

static int receive_flac_loop(int fd, AudioPlayback *pb, const AudioConfig *cfg)
{
    size_t  comp_cap = (size_t)cfg->chunk_size * 2;
    uint8_t *comp_buf = malloc(comp_cap);
    if (!comp_buf) return -1;

    while (atomic_load(&g_app.is_receiving)) {
        uint8_t hdr[4];
        if (read_fully(fd, hdr, 4) != 4) {
            if (atomic_load(&g_app.is_receiving))
                ui_update_status("Streamer disconnected");
            break;
        }

        uint32_t frame_len = read_be32(hdr);
        if (frame_len == 0 || frame_len > comp_cap) {
            LOG_W("Invalid FLAC frame length: %u", frame_len);
            continue;
        }

        if (read_fully(fd, comp_buf, frame_len) != (ssize_t)frame_len)
            break;

        audio_playback_write(pb, comp_buf, frame_len);

        atomic_fetch_add(&g_app.total_bytes_sent, (int64_t)(frame_len + 4));
        atomic_fetch_add(&g_app.bytes_sent_this_second, (int64_t)(frame_len + 4));

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

    free(comp_buf);
    return 0;
}

/* ---- Receive thread ---- */

static void *receive_thread_func(void *arg)
{
    (void)arg;
    LOG_I("Receive thread started - connecting to %s", rctx.server_ip);

    int fd = net_connect(rctx.server_ip, AUDIO_PORT, 5000);
    if (fd < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Cannot connect to %s:%d", rctx.server_ip, AUDIO_PORT);
        ui_update_status(msg);
        atomic_store(&g_app.is_receiving, false);
        ui_reset();
        return NULL;
    }

    rctx.socket_fd = fd;

    AudioConfig cfg;
    int hrc = protocol_read_header(fd, &cfg);
    if (hrc != 0) {
        ui_update_status("Invalid stream format");
        net_close(&fd);
        rctx.socket_fd = -1;
        atomic_store(&g_app.is_receiving, false);
        ui_reset();
        return NULL;
    }

    rctx.cfg = cfg;

    /* Update UI with format info */
    {
        char fmt[256], sr[64], status[256];
        config_format_string(&cfg, fmt, sizeof(fmt));
        config_sample_rate_string(&cfg, sr, sizeof(sr));

        snprintf(status, sizeof(status),
                 "Receiving %s %s from %s",
                 sr, cfg.channels == 1 ? "Mono" : "Stereo", rctx.server_ip);
        ui_update_status(status);
        ui_show_receiving(rctx.server_ip);
        ui_update_format_info(sr, fmt);
    }

    /* Start sub-services */
    ping_client_start(rctx.server_ip);
    chat_client_start(rctx.server_ip);

    /* Open playback */
    AudioPlayback *pb = audio_playback_open(&cfg);
    if (!pb) {
        ui_update_status("Failed to open audio playback");
        goto cleanup;
    }

    atomic_store(&g_app.stream_start_time, current_time_ms());
    atomic_store(&g_app.last_time_ms, current_time_ms());
    atomic_store(&g_app.total_bytes_sent, 0);
    atomic_store(&g_app.bytes_sent_this_second, 0);

    if (cfg.use_flac)
        receive_flac_loop(fd, pb, &cfg);
    else
        receive_pcm_loop(fd, pb, &cfg);

    audio_playback_close(pb);

cleanup:
    ping_client_stop();
    chat_client_stop();
    net_close(&fd);
    rctx.socket_fd = -1;
    atomic_store(&g_app.is_receiving, false);
    ui_reset();
    ui_update_status("Receiving stopped");

    LOG_I("Receive thread stopped");
    return NULL;
}

/* ---- Public API ---- */

int receiving_start(const char *server_ip)
{
    memset(&rctx, 0, sizeof(rctx));
    rctx.socket_fd = -1;
    snprintf(rctx.server_ip, INET_ADDRSTRLEN, "%s", server_ip);

    atomic_store(&g_app.is_receiving, true);
    atomic_store(&g_app.current_latency_ms, -1);
    atomic_store(&g_app.total_bytes_sent, 0);

    rctx.running = true;

    if (pthread_create(&rctx.receive_thread, NULL, receive_thread_func, NULL) != 0) {
        LOG_E("pthread_create(receive): %s", strerror(errno));
        atomic_store(&g_app.is_receiving, false);
        return -1;
    }

    return 0;
}

void receiving_stop(void)
{
    if (!atomic_exchange(&g_app.is_receiving, false))
        return;

    LOG_I("Stopping receiving...");
    ui_update_status("Stopping...");

    ping_client_stop();
    chat_client_stop();
    net_close(&rctx.socket_fd);

    if (rctx.running) {
        pthread_join(rctx.receive_thread, NULL);
        rctx.running = false;
    }
}