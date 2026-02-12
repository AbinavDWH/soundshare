#include "audio.h"

/* ---- Capture (record monitor source) ---- */

struct AudioCapture {
    pa_simple    *pa;
    AudioConfig   cfg;
    char          source_name[256];
};

int audio_get_monitor_source(char *buf, size_t len)
{
    /* Use the PulseAudio threaded main-loop API to query the
       default sink and append ".monitor".                         */

    pa_mainloop     *ml  = NULL;
    pa_context      *ctx = NULL;
    int              found = -1;

    ml = pa_mainloop_new();
    if (!ml) goto done;

    ctx = pa_context_new(pa_mainloop_get_api(ml), "soundshare-query");
    if (!ctx) goto done;

    if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
        goto done;

    /* Run until connected */
    while (1) {
        pa_mainloop_iterate(ml, 1, NULL);
        pa_context_state_t st = pa_context_get_state(ctx);
        if (st == PA_CONTEXT_READY)  break;
        if (!PA_CONTEXT_IS_GOOD(st)) goto done;
    }

    /* Request server info to get default sink name */
    typedef struct { char *name; int done; } Info;
    Info info = { NULL, 0 };

    pa_context_get_server_info(ctx,
        (pa_server_info_cb_t)(void (*)(pa_context*,const pa_server_info*,void*)){
            NULL  /* replaced below */
        }, &info);

    /* Because function-pointer casts are ugly, use a static callback: */
    /* We will use a simpler approach below */

    pa_context_disconnect(ctx);
    pa_context_unref(ctx); ctx = NULL;
    pa_mainloop_free(ml);  ml  = NULL;

    /* Simpler approach: use pacmd or pactl via popen */
    {
        FILE *fp = popen("pactl get-default-sink 2>/dev/null", "r");
        if (fp) {
            char sink[256] = {0};
            if (fgets(sink, sizeof(sink), fp)) {
                /* Remove trailing newline */
                size_t slen = strlen(sink);
                while (slen > 0 && (sink[slen-1] == '\n' || sink[slen-1] == '\r'))
                    sink[--slen] = '\0';

                snprintf(buf, len, "%s.monitor", sink);
                found = 0;
            }
            pclose(fp);
        }
    }

    if (found != 0) {
        /* Fallback: try to find any monitor source */
        FILE *fp = popen(
            "pactl list short sources 2>/dev/null | grep '\\.monitor' | head -1 | awk '{print $2}'",
            "r");
        if (fp) {
            char src[256] = {0};
            if (fgets(src, sizeof(src), fp)) {
                size_t slen = strlen(src);
                while (slen > 0 && (src[slen-1] == '\n' || src[slen-1] == '\r'))
                    src[--slen] = '\0';
                if (slen > 0) {
                    snprintf(buf, len, "%s", src);
                    found = 0;
                }
            }
            pclose(fp);
        }
    }

done:
    if (ctx) { pa_context_disconnect(ctx); pa_context_unref(ctx); }
    if (ml)  pa_mainloop_free(ml);

    if (found != 0)
        LOG_E("Could not find monitor source");

    return found;
}

AudioCapture *audio_capture_open(const AudioConfig *cfg)
{
    AudioCapture *cap = calloc(1, sizeof(*cap));
    if (!cap) return NULL;

    cap->cfg = *cfg;

    if (audio_get_monitor_source(cap->source_name, sizeof(cap->source_name)) != 0) {
        free(cap);
        return NULL;
    }
    LOG_I("Capture source: %s", cap->source_name);

    /* Determine pa_sample_format */
    pa_sample_format_t fmt;
    if (cfg->is_float)
        fmt = PA_SAMPLE_FLOAT32LE;
    else if (cfg->bits_per_sample >= 24)
        fmt = PA_SAMPLE_S32LE;
    else
        fmt = PA_SAMPLE_S16LE;

    pa_sample_spec ss = {
        .format   = fmt,
        .rate     = (uint32_t)cfg->sample_rate,
        .channels = (uint8_t)cfg->channels,
    };

    pa_buffer_attr ba = {
        .maxlength = (uint32_t)-1,
        .fragsize  = (uint32_t)cfg->chunk_size,
        .tlength   = (uint32_t)-1,
        .prebuf    = (uint32_t)-1,
        .minreq    = (uint32_t)-1,
    };

    int err = 0;
    cap->pa = pa_simple_new(
        NULL,                 /* server */
        "SoundShare",         /* app name */
        PA_STREAM_RECORD,
        cap->source_name,     /* device */
        "System audio",       /* description */
        &ss,
        NULL,                 /* channel map */
        &ba,
        &err
    );

    if (!cap->pa) {
        LOG_E("pa_simple_new(record): %s", pa_strerror(err));
        free(cap);
        return NULL;
    }

    LOG_I("Capture opened: %dHz %dch %s",
          cfg->sample_rate, cfg->channels, cfg->pa_format);
    return cap;
}

int audio_capture_read(AudioCapture *cap, void *buf, size_t len)
{
    int err = 0;
    if (pa_simple_read(cap->pa, buf, len, &err) < 0) {
        LOG_E("pa_simple_read: %s", pa_strerror(err));
        return -1;
    }
    return (int)len;
}

void audio_capture_close(AudioCapture *cap)
{
    if (!cap) return;
    if (cap->pa) pa_simple_free(cap->pa);
    free(cap);
}

/* ---- Playback ---- */

struct AudioPlayback {
    pa_simple   *pa;
    AudioConfig  cfg;
};

AudioPlayback *audio_playback_open(const AudioConfig *cfg)
{
    AudioPlayback *pb = calloc(1, sizeof(*pb));
    if (!pb) return NULL;
    pb->cfg = *cfg;

    pa_sample_format_t fmt;
    if (cfg->is_float)
        fmt = PA_SAMPLE_FLOAT32LE;
    else if (cfg->bits_per_sample >= 24)
        fmt = PA_SAMPLE_S32LE;
    else
        fmt = PA_SAMPLE_S16LE;

    pa_sample_spec ss = {
        .format   = fmt,
        .rate     = (uint32_t)cfg->sample_rate,
        .channels = (uint8_t)cfg->channels,
    };

    pa_buffer_attr ba = {
        .maxlength = (uint32_t)-1,
        .fragsize  = (uint32_t)-1,
        .tlength   = (uint32_t)(cfg->chunk_size * 2),
        .prebuf    = (uint32_t)cfg->chunk_size,
        .minreq    = (uint32_t)-1,
    };

    int err = 0;
    pb->pa = pa_simple_new(
        NULL,
        "SoundShare",
        PA_STREAM_PLAYBACK,
        NULL,                   /* default sink */
        "Network audio",
        &ss,
        NULL,
        &ba,
        &err
    );

    if (!pb->pa) {
        LOG_E("pa_simple_new(playback): %s", pa_strerror(err));
        free(pb);
        return NULL;
    }

    LOG_I("Playback opened: %dHz %dch %s",
          cfg->sample_rate, cfg->channels, cfg->pa_format);
    return pb;
}

int audio_playback_write(AudioPlayback *pb, const void *buf, size_t len)
{
    int err = 0;
    if (pa_simple_write(pb->pa, buf, len, &err) < 0) {
        LOG_E("pa_simple_write: %s", pa_strerror(err));
        return -1;
    }
    return 0;
}

void audio_playback_close(AudioPlayback *pb)
{
    if (!pb) return;
    if (pb->pa) {
        pa_simple_drain(pb->pa, NULL);
        pa_simple_free(pb->pa);
    }
    free(pb);
}