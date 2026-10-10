#include "psx.h"

void psx_raw16_to_rgba(const uint8_t *src, int w, int h, uint8_t *dst, int transparent_black) {
    int i, n = w * h;
    for (i = 0; i < n; ++i) psx_color_to_rgba(rd16(src + i * 2), dst + i * 4, transparent_black);
}

void psx_indexed8_to_rgba(const uint8_t *idx, int w, int h, const uint8_t *pal, uint8_t *dst) {
    int i, n = w * h;
    for (i = 0; i < n; ++i) psx_color_to_rgba(rd16(pal + idx[i] * 2), dst + i * 4, 1);
}

void psx_indexed4_to_rgba(const uint8_t *idx, int w, int h, const uint8_t *pal, uint8_t *dst) {
    int i, n = w * h;
    for (i = 0; i < n; ++i) {
        int v = (i & 1) ? (idx[i >> 1] >> 4) : (idx[i >> 1] & 15);
        psx_color_to_rgba(rd16(pal + v * 2), dst + i * 4, 1);
    }
}

void psx_vram_clear(psx_vram_t *v) { memset(v, 0, sizeof(*v)); }

void psx_vram_load(psx_vram_t *v, int x, int y, int w, int h, const uint8_t *data) {
    int i, j;
    for (j = 0; j < h; ++j) {
        if (y + j < 0 || y + j >= 512) continue;
        for (i = 0; i < w; ++i) {
            if (x + i < 0 || x + i >= 1024) continue;
            v->px[y + j][x + i] = rd16(data + ((size_t)j * w + i) * 2);
        }
    }
}

static inline uint16_t vram_at(const psx_vram_t *v, int x, int y) {
    return v->px[y & 511][x & 1023];
}

void psx_vram_decode(const psx_vram_t *v, uint16_t tpage, uint16_t clut, int u, int vv, int w, int h, uint8_t *rgba) {
    int tpx = (tpage & 15) * 64, tpy = ((tpage >> 4) & 1) * 256, bpp = (tpage >> 7) & 3;
    int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 0x1ff;
    int x, y;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            int tu = u + x, tv = vv + y;
            uint16_t c;
            if (bpp == 0) {
                uint16_t word = vram_at(v, tpx + tu / 4, tpy + tv);
                c = vram_at(v, cx + ((word >> (4 * (tu & 3))) & 15), cy);
            } else if (bpp == 1) {
                uint16_t word = vram_at(v, tpx + tu / 2, tpy + tv);
                c = vram_at(v, cx + ((word >> (8 * (tu & 1))) & 255), cy);
            } else {
                c = vram_at(v, tpx + tu, tpy + tv);
            }
            psx_color_to_rgba(c, rgba + ((size_t)y * w + x) * 4, 1);
        }
    }
}

void psx_vram_to_rgba(const psx_vram_t *v, uint8_t *rgba) {
    int x, y;
    for (y = 0; y < 512; ++y)
        for (x = 0; x < 1024; ++x)
            psx_color_to_rgba(v->px[y][x], rgba + ((size_t)y * 1024 + x) * 4, 0);
}
