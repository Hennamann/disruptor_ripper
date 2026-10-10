#ifndef PNG_H
#define PNG_H

#include "common.h"

/* Write an 8-bit RGBA image as PNG (zlib "stored" blocks, no external dependency). Returns 1 on success. */
int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba);

#endif
