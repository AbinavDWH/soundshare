#ifndef CONFIG_H
#define CONFIG_H

#include "soundshare.h"

#define NUM_PRESETS 7

/* Quality preset names */
extern const char *QUALITY_NAMES[NUM_PRESETS];

/* Audio configuration */
typedef struct {
    int  sample_rate;
    int  channels;
    int  frames_per_buffer;
    int  bits_per_sample;
    int  bytes_per_sample;
    int  compression_type;   /* 0 = PCM, 1 = FLAC */
    bool is_float;
    bool is_hires;
    bool use_flac;
    int  chunk_size;
    int  socket_buffer_size;
    int  preset_index;

    /* PulseAudio format string */
    const char *pa_format;
} AudioConfig;

/* Preset raw data: {sampleRate, channels, framesPerBuf, bitsPerSample, compression, isFloat} */
typedef struct {
    int sample_rate;
    int channels;
    int frames_per_buffer;
    int bits_per_sample;
    int compression;
    int is_float;
} PresetData;

extern const PresetData PRESETS[NUM_PRESETS];

/* Build an AudioConfig from a preset index */
void config_load_preset(AudioConfig *cfg, int preset_index);

/* Build an AudioConfig from raw header values */
void config_from_header(AudioConfig *cfg, int sr, int ch, int fpb,
                        int bps, int comp, int float_flag);

/* Info strings (caller must not free — uses static buffers or small alloc) */
double  config_buffer_latency_ms(const AudioConfig *cfg);
int64_t config_raw_bitrate_kbps(const AudioConfig *cfg);
void    config_format_string(const AudioConfig *cfg, char *buf, size_t len);
void    config_sample_rate_string(const AudioConfig *cfg, char *buf, size_t len);
void    config_channel_string(const AudioConfig *cfg, char *buf, size_t len);
void    config_compression_string(const AudioConfig *cfg, char *buf, size_t len);

/* Capability detection */
typedef struct {
    bool supports_96khz;
    bool supports_192khz;
    bool supports_24bit;
    bool supports_32bit;
    bool supports_float;
    bool supports_flac_encode;
    bool supports_flac_decode;
    int  max_sample_rate;
    int  max_bit_depth;
} DeviceCapabilities;

void config_detect_capabilities(DeviceCapabilities *caps);
bool config_is_hires_capable(const DeviceCapabilities *caps);

#endif /* CONFIG_H */