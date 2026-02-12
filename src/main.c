#include "soundshare.h"
#include "config.h"
#include "ui.h"

#include <stdarg.h>
#include <sys/time.h>

/* ---- Global application state ---- */
AppState g_app;

/* ---- Logging ---- */
static const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void ss_log(LogLevel level, const char *fmt, ...)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    fprintf(stderr, "[%02d:%02d:%02d.%03ld] [%s] ",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            tv.tv_usec / 1000, level_str[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- Time helpers ---- */
int64_t current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t current_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ---- App state ---- */
void app_state_init(void)
{
    memset(&g_app, 0, sizeof(g_app));
    atomic_store(&g_app.is_streaming, false);
    atomic_store(&g_app.is_receiving, false);
    atomic_store(&g_app.shutdown_requested, false);
    atomic_store(&g_app.bytes_sent_this_second, 0);
    atomic_store(&g_app.total_bytes_sent, 0);
    atomic_store(&g_app.last_time_ms, 0);
    atomic_store(&g_app.stream_start_time, 0);
    atomic_store(&g_app.current_latency_ms, -1);
    atomic_store(&g_app.receiver_count, 0);
    g_app.selected_preset = 2;
    pthread_mutex_init(&g_app.lock, NULL);
}

void app_state_destroy(void)
{
    pthread_mutex_destroy(&g_app.lock);
}

/* ---- Signal handler ---- */
static void signal_handler(int sig)
{
    (void)sig;
    atomic_store(&g_app.shutdown_requested, true);
}

/* ---- main ---- */
int main(int argc, char **argv)
{
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    app_state_init();

    LOG_I("SoundShare v%s starting", SS_VERSION);

    int ret = ui_run(argc, argv);

    app_state_destroy();
    return ret;
}