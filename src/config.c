#include "config.h"
#include <stdio.h>

const char *QUALITY_NAMES[NUM_PRESETS] = {
    "Ultra Low  – 44.1 kHz Mono 16-bit",
    "Low Latency – 44.1 kHz Stereo 16-bit",
    "Balanced    – 48 kHz Stereo 16-bit",
    "High Quality – 48 kHz Stereo 24-bit",
    "Maximum     – 48 kHz Stereo 24-bit",
    "Hi-Res      – 96 kHz Stereo 24-bit",
    "Hi-Res Ultra – 192 kHz Stereo 24-bit",
};

const PresetData PRESETS[NUM_PRESETS] = {
    { 44100, 1,     32, 16, 0, 0 },
    { 44100, 2,     32, 16, 0, 0 },
    { 48000, 2,    240, 16, 0, 0 },
    { 48000, 2,   4800, 24, 0, 0 },
    { 48000, 2,   9600, 24, 0, 0 },
    { 96000, 2,  96000, 24, 0, 0 },
    {192000, 2, 192000, 24, 0, 0 },
};

/* ------------------------------------------------------------------ */
static void config_compute_derived(AudioConfig *cfg)
{
    /* bytes per sample & PA format */
    if (cfg->is_float) {
        cfg->bytes_per_sample = 4;
        cfg->pa_format = "float32le";
    } else if (cfg->bits_per_sample >= 24) {
        cfg->bytes_per_sample = 4;       /* 24-bit stored in 32-bit container */
        cfg->pa_format = "s32le";
    } else {
        cfg->bytes_per_sample = 2;
        cfg->pa_format = "s16le";
    }

    cfg->use_flac = (cfg->compression_type == 1);
    cfg->is_hires = (cfg->sample_rate > 48000 ||
                     cfg->bits_per_sample > 24 ||
                     (cfg->bits_per_sample == 24 && cfg->sample_rate >= 96000));

    cfg->chunk_size = cfg->frames_per_buffer *
                      cfg->channels *
                      cfg->bytes_per_sample;

    if (cfg->is_hires)
        cfg->socket_buffer_size = cfg->chunk_size * 4;
    else if (cfg->use_flac)
        cfg->socket_buffer_size = cfg->chunk_size * 2;
    else
        cfg->socket_buffer_size = cfg->chunk_size * (cfg->preset_index <= 1 ? 2 : 4);
}

void config_load_preset(AudioConfig *cfg, int idx)
{
    if (idx < 0 || idx >= NUM_PRESETS) idx = 2;

    const PresetData *p = &PRESETS[idx];

    memset(cfg, 0, sizeof(*cfg));
    cfg->preset_index      = idx;
    cfg->sample_rate       = p->sample_rate;
    cfg->channels          = p->channels;
    cfg->frames_per_buffer = p->frames_per_buffer;
    cfg->bits_per_sample   = p->bits_per_sample;
    cfg->compression_type  = p->compression;
    cfg->is_float          = (p->is_float != 0);

    config_compute_derived(cfg);
}

void config_from_header(AudioConfig *cfg, int sr, int ch, int fpb,
                        int bps, int comp, int float_flag)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate       = sr;
    cfg->channels          = ch;
    cfg->frames_per_buffer = fpb;
    cfg->bits_per_sample   = bps;
    cfg->compression_type  = comp;
    cfg->is_float          = (float_flag != 0);
    cfg->preset_index      = (comp == 1) ? 7 : (sr > 48000 ? 5 : 2);

    config_compute_derived(cfg);
}

/* ------------------------------------------------------------------ */
double config_buffer_latency_ms(const AudioConfig *cfg)
{
    return (cfg->frames_per_buffer * 1000.0) / cfg->sample_rate;
}

int64_t config_raw_bitrate_kbps(const AudioConfig *cfg)
{
    return ((int64_t)cfg->sample_rate * cfg->channels * cfg->bits_per_sample) / 1000;
}

void config_format_string(const AudioConfig *cfg, char *buf, size_t len)
{
    const char *codec  = cfg->use_flac ? "FLAC" : "PCM";
    const char *fl     = cfg->is_float ? " Float" : "";
    const char *hi     = cfg->is_hires ? " [Hi-Res]" : "";
    const char *ch     = cfg->channels == 1 ? "Mono" : "Stereo";
    double mbps = (double)cfg->sample_rate * cfg->channels *
                  cfg->bits_per_sample / 1e6;

    snprintf(buf, len, "%s %d-bit%s %s (%.1f Mbps raw)%s",
             codec, cfg->bits_per_sample, fl, ch, mbps, hi);
}

void config_sample_rate_string(const AudioConfig *cfg, char *buf, size_t len)
{
    if (cfg->sample_rate >= 1000)
        snprintf(buf, len, "%.1f kHz", cfg->sample_rate / 1000.0);
    else
        snprintf(buf, len, "%d Hz", cfg->sample_rate);
}

void config_channel_string(const AudioConfig *cfg, char *buf, size_t len)
{
    snprintf(buf, len, "%s", cfg->channels == 1 ? "Mono" : "Stereo");
}

void config_compression_string(const AudioConfig *cfg, char *buf, size_t len)
{
    if (cfg->use_flac && cfg->is_hires)
        snprintf(buf, len, "Hi-Res FLAC Lossless");
    else if (cfg->use_flac)
        snprintf(buf, len, "FLAC Lossless");
    else if (cfg->is_hires)
        snprintf(buf, len, "Hi-Res PCM");
    else
        snprintf(buf, len, "Uncompressed PCM");
}

/* ---- Capability detection ---- */

void config_detect_capabilities(DeviceCapabilities *caps)
{
    memset(caps, 0, sizeof(*caps));

    /* On Linux with PulseAudio most formats are supported via resampling.
       We do conservative defaults here.                                   */
    caps->supports_96khz  = true;
    caps->supports_192khz = true;
    caps->supports_24bit  = true;
    caps->supports_32bit  = true;
    caps->supports_float  = true;

#ifdef HAVE_FLAC
    caps->supports_flac_encode = true;
    caps->supports_flac_decode = true;
#else
    caps->supports_flac_encode = false;
    caps->supports_flac_decode = false;
#endif

    caps->max_sample_rate = caps->supports_192khz ? 192000 :
                            caps->supports_96khz  ?  96000 : 48000;
    caps->max_bit_depth   = caps->supports_32bit  ?     32 :
                            caps->supports_24bit  ?     24 : 16;
}

bool config_is_hires_capable(const DeviceCapabilities *caps)
{
    return caps->supports_96khz && caps->supports_24bit;
}