#ifndef ADPCM_H
#define ADPCM_H

#include "common.h"

/* Write raw SPU-ADPCM data as a .vag file (big-endian header). */
int vag_write(const char *path, const uint8_t *data, size_t n, uint32_t sample_rate, const char *name);

/* Decode raw SPU-ADPCM data (16-byte blocks) and write a 16-bit mono .wav. */
int adpcm_write_wav(const char *path, const uint8_t *data, size_t n, uint32_t sample_rate, int stop_at_end_flag);

#endif
