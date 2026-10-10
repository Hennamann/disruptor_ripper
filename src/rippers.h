#ifndef RIPPERS_H
#define RIPPERS_H

#include "wad.h"

/* sprites.c */
void rip_enemy_sprites(wad_t *w);
void rip_weapons(wad_t *w);
void rip_psyonics(wad_t *w);
void rip_hud(wad_t *w);

/* sounds.c */
void rip_sounds(wad_t *w);
void rip_music(wad_t *w);
void rip_fmv_audio(wad_t *w);

/* level.c */
void rip_levels(wad_t *w, int want_textures, int want_geometry);

/* misc.c */
void rip_splash(wad_t *w);
void rip_title(wad_t *w);
void rip_banners(wad_t *w);
void rip_warning_screen(wad_t *w);
void rip_title_text(wad_t *w);
void rip_exe_strings(const char *exe_path);

/* level.c */
void rip_text_levels(wad_t *w);

/* Shared helpers (sprites.c) */
/* Write an 8bpp indexed image using a 256-entry 15-bit palette; index 0 is transparent. */
void write_indexed8_png(const char *path, const uint8_t *idx, int w, int h, const uint8_t *pal, int transparent0);
/* Write a 4bpp image (two pixels per byte, low nibble first) with a 16-entry palette. */
void write_indexed4_png(const char *path, const uint8_t *idx, int w, int h, const uint8_t *pal);
/* Decompress one RLE sprite frame (format used by enemy/weapon frames). */
void sprite_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);

#endif
