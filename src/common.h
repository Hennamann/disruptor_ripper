#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Little-endian readers that never perform unaligned/strict-aliasing loads. */
static inline uint16_t rd16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | (b[1] << 8));
}
static inline uint32_t rd32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline int32_t rds32(const void *p) { return (int32_t)rd32(p); }
static inline int16_t rds16(const void *p) { return (int16_t)rd16(p); }

static inline void wr16(void *p, uint16_t v) { uint8_t *b = (uint8_t *)p; b[0] = v & 0xff; b[1] = v >> 8; }
static inline void wr32(void *p, uint32_t v) { uint8_t *b = (uint8_t *)p; b[0] = v & 0xff; b[1] = (v >> 8) & 0xff; b[2] = (v >> 16) & 0xff; b[3] = v >> 24; }
static inline void wr32be(void *p, uint32_t v) { uint8_t *b = (uint8_t *)p; b[3] = v & 0xff; b[2] = (v >> 8) & 0xff; b[1] = (v >> 16) & 0xff; b[0] = v >> 24; }

#define PATH_MAX_LEN 1024

/* Verbosity / helpers implemented in main.c */
void die(const char *fmt, ...);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
FILE *xfopen(const char *path, const char *mode);

#endif
