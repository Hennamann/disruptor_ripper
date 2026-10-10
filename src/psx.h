#ifndef PSX_H
#define PSX_H

#include "common.h"

/* 15-bit PlayStation colour (BGR555 + semi-transparency bit) to RGBA.
 * 0x0000 is fully transparent; a set STP bit on a non-black colour gives alpha 128. */
static inline void psx_color_to_rgba(uint16_t c, uint8_t *dst, int transparent_black) {
    dst[0] = (uint8_t)(((c      ) & 31) * 255 / 31);
    dst[1] = (uint8_t)(((c >>  5) & 31) * 255 / 31);
    dst[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
    if (c == 0 && transparent_black)       dst[3] = 0;
    else if ((c & 0x8000) && (c & 0x7fff)) dst[3] = 128;
    else                                   dst[3] = 255;
}

/* Convert a raw 16bpp VRAM-style image (w words wide) into RGBA. */
void psx_raw16_to_rgba(const uint8_t *src, int w, int h, uint8_t *dst, int transparent_black);

/* Convert an 8bpp indexed image with a 256-entry 15-bit palette into RGBA. */
void psx_indexed8_to_rgba(const uint8_t *idx, int w, int h, const uint8_t *pal, uint8_t *dst);
/* Same for a 4bpp (low nibble first) image with a 16-entry palette. */
void psx_indexed4_to_rgba(const uint8_t *idx, int w, int h, const uint8_t *pal, uint8_t *dst);

/* Software model of the 1024x512 16-bit VRAM. */
typedef struct psx_vram_s {
    uint16_t px[512][1024];
} psx_vram_t;

void psx_vram_clear(psx_vram_t *v);
/* Emulates LoadImage(): copy a w x h block of 16-bit words to (x, y). */
void psx_vram_load(psx_vram_t *v, int x, int y, int w, int h, const uint8_t *data);
/* Decode a texture rectangle (in texel units) using a standard tpage id and clut id. */
void psx_vram_decode(const psx_vram_t *v, uint16_t tpage, uint16_t clut, int u, int vv, int w, int h, uint8_t *rgba);
/* Dump the whole VRAM as RGB (16-bit interpretation). rgba must hold 1024*512*4 bytes. */
void psx_vram_to_rgba(const psx_vram_t *v, uint8_t *rgba);

#endif
