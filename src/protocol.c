#include "protocol.h"
#include "network.h"

#include <unistd.h>
#include <errno.h>

/* ---- Byte-order helpers ---- */

void write_be32(uint8_t *dst, uint32_t val)
{
    dst[0] = (uint8_t)(val >> 24);
    dst[1] = (uint8_t)(val >> 16);
    dst[2] = (uint8_t)(val >>  8);
    dst[3] = (uint8_t)(val);
}

void write_be16(uint8_t *dst, uint16_t val)
{
    dst[0] = (uint8_t)(val >> 8);
    dst[1] = (uint8_t)(val);
}

uint32_t read_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] <<  8) |
           ((uint32_t)src[3]);
}

uint16_t read_be16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) |
           ((uint16_t)src[1]);
}

/* ---- Guaranteed I/O ---- */

ssize_t read_fully(int fd, void *buf, size_t count)
{
    size_t  total = 0;
    uint8_t *p    = (uint8_t *)buf;

    while (total < count) {
        ssize_t n = read(fd, p + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;          /* EOF */
        total += (size_t)n;
    }
    return (ssize_t)total;
}

ssize_t write_fully(int fd, const void *buf, size_t count)
{
    size_t        total = 0;
    const uint8_t *p    = (const uint8_t *)buf;

    while (total < count) {
        ssize_t n = write(fd, p + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

/* ---- Sample-rate validation ---- */

bool protocol_valid_sample_rate(int sr)
{
    return sr == 44100  || sr == 48000 ||
           sr == 88200  || sr == 96000 ||
           sr == 176400 || sr == 192000;
}

/* ---- Header I/O ---- */

int protocol_write_header(int fd, const AudioConfig *cfg)
{
    uint8_t hdr[HEADER_SIZE];

    write_be32(hdr +  0, HEADER_MAGIC);
    write_be32(hdr +  4, HEADER_VERSION);
    write_be32(hdr +  8, (uint32_t)cfg->sample_rate);
    write_be16(hdr + 12, (uint16_t)cfg->bits_per_sample);
    write_be16(hdr + 14, (uint16_t)cfg->channels);
    write_be32(hdr + 16, (uint32_t)cfg->frames_per_buffer);
    write_be32(hdr + 20, (uint32_t)cfg->chunk_size);
    write_be16(hdr + 24, (uint16_t)cfg->compression_type);
    hdr[26] = cfg->is_float ? 1 : 0;
    hdr[27] = 0; /* reserved */

    if (write_fully(fd, hdr, HEADER_SIZE) != HEADER_SIZE) {
        LOG_E("protocol_write_header: write failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int protocol_read_header(int fd, AudioConfig *cfg)
{
    uint8_t hdr[HEADER_SIZE];

    if (read_fully(fd, hdr, HEADER_SIZE) != HEADER_SIZE) {
        LOG_E("protocol_read_header: read failed: %s", strerror(errno));
        return -1;
    }

    uint32_t magic   = read_be32(hdr + 0);
    uint32_t version = read_be32(hdr + 4);
    int sr   = (int)read_be32(hdr + 8);
    int bps  = (int)read_be16(hdr + 12);
    int ch   = (int)read_be16(hdr + 14);
    int fpb  = (int)read_be32(hdr + 16);
    int cs   = (int)read_be32(hdr + 20);
    int comp = (int)read_be16(hdr + 24);
    int fl   = hdr[26];

    if (magic != HEADER_MAGIC) {
        LOG_E("Bad magic: 0x%08X", magic);
        return -2;
    }

    if (!protocol_valid_sample_rate(sr)) {
        LOG_E("Invalid sample rate: %d", sr);
        return -2;
    }
    if (bps != 16 && bps != 24 && bps != 32) {
        LOG_E("Invalid bits per sample: %d", bps);
        return -2;
    }
    if (ch != 1 && ch != 2) {
        LOG_E("Invalid channels: %d", ch);
        return -2;
    }
    if (comp != 0 && comp != 1) {
        LOG_E("Invalid compression: %d", comp);
        return -2;
    }

    LOG_I("Header v%u: %dHz %dch %dbit comp=%d float=%d cs=%d",
          version, sr, ch, bps, comp, fl, cs);

    config_from_header(cfg, sr, ch, fpb, bps, comp, fl);
    /* override chunk_size with the value the sender actually uses */
    (void)cs;

    return 0;
}