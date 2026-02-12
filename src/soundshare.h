#ifndef SOUNDSHARE_H
#define SOUNDSHARE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#define SS_TAG "SoundShare"
#define SS_VERSION "1.0.0"

/* Logging */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

void ss_log(LogLevel level, const char *fmt, ...);

#define LOG_D(...) ss_log(LOG_DEBUG, __VA_ARGS__)
#define LOG_I(...) ss_log(LOG_INFO,  __VA_ARGS__)
#define LOG_W(...) ss_log(LOG_WARN,  __VA_ARGS__)
#define LOG_E(...) ss_log(LOG_ERROR, __VA_ARGS__)

/* Global app state */
typedef struct {
    atomic_bool is_streaming;
    atomic_bool is_receiving;
    atomic_bool shutdown_requested;

    atomic_long bytes_sent_this_second;
    atomic_long total_bytes_sent;
    atomic_long last_time_ms;
    atomic_long stream_start_time;
    atomic_long current_latency_ms;
    atomic_int  receiver_count;

    int selected_preset;

    pthread_mutex_t lock;
} AppState;

extern AppState g_app;

/* Timestamp helpers */
int64_t current_time_ms(void);
int64_t current_time_ns(void);

/* Init / cleanup */
void app_state_init(void);
void app_state_destroy(void);

#endif /* SOUNDSHARE_H */