#ifndef AUDIO_H
#define AUDIO_H

#include "soundshare.h"
#include "config.h"

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>

/* Opaque handles */
typedef struct AudioCapture  AudioCapture;
typedef struct AudioPlayback AudioPlayback;

/**
 * Open a PulseAudio recording stream that captures the monitor
 * (system audio output).
 * Returns NULL on failure.
 */
AudioCapture *audio_capture_open(const AudioConfig *cfg);

/**
 * Read exactly cfg->chunk_size bytes of PCM into `buf`.
 * Returns  the number of bytes read (== chunk_size), or -1 on error.
 */
int audio_capture_read(AudioCapture *cap, void *buf, size_t len);

/**
 * Close and free the capture stream.
 */
void audio_capture_close(AudioCapture *cap);

/**
 * Open a PulseAudio playback stream.
 * Returns NULL on failure.
 */
AudioPlayback *audio_playback_open(const AudioConfig *cfg);

/**
 * Write `len` bytes of PCM to playback.
 * Returns 0 on success, -1 on error.
 */
int audio_playback_write(AudioPlayback *pb, const void *buf, size_t len);

/**
 * Drain pending samples, then close.
 */
void audio_playback_close(AudioPlayback *pb);

/**
 * Get the monitor source name for the default sink.
 * Writes into `buf`.  Returns 0 on success.
 */
int audio_get_monitor_source(char *buf, size_t len);

#endif /* AUDIO_H */