#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "soundshare.h"
#include "config.h"
#include <sys/types.h>

#define HEADER_MAGIC    0x53534844
#define HEADER_VERSION  2
#define HEADER_SIZE     28

#define AUDIO_PORT  5000
#define PING_PORT   5001
#define CHAT_PORT   5002

#define PING_REQUEST   0x01
#define PING_RESPONSE  0x02
#define LATENCY_REPORT 0x03
#define CHAT_MSG       0x10

int protocol_write_header(int fd, const AudioConfig *cfg);
int protocol_read_header(int fd, AudioConfig *cfg);

void     write_be32(uint8_t *dst, uint32_t val);
void     write_be16(uint8_t *dst, uint16_t val);
uint32_t read_be32(const uint8_t *src);
uint16_t read_be16(const uint8_t *src);

ssize_t  read_fully(int fd, void *buf, size_t count);
ssize_t  write_fully(int fd, const void *buf, size_t count);

bool     protocol_valid_sample_rate(int sr);

#endif
