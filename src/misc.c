/* Boot splash screens, level name banners and the title/menu graphics block. */
#include "rippers.h"
#include "png.h"
#include "psx.h"
#include "adpcm.h"
#include "platform.h"

#define TITLE_BLOCK_SIZE 0x92000

static void save_raw16(wad_t *w, uint64_t off, int wdt, int hgt, const char *subdir, const char *name, int transparent) {
    size_t n = (size_t)wdt * hgt * 2;
    uint8_t *raw = wad_read_alloc(w, off, n), *rgba = xmalloc((size_t)wdt * hgt * 4);
    char path[PATH_MAX_LEN];
    psx_raw16_to_rgba(raw, wdt, hgt, rgba, transparent);
    out_path(path, sizeof(path), subdir, name);
    png_write_rgba(path, wdt, hgt, rgba);
    free(raw); free(rgba);
}

static void save_mem16(const uint8_t *raw, int wdt, int hgt, const char *subdir, const char *name, int transparent) {
    uint8_t *rgba = xmalloc((size_t)wdt * hgt * 4);
    char path[PATH_MAX_LEN];
    psx_raw16_to_rgba(raw, wdt, hgt, rgba, transparent);
    out_path(path, sizeof(path), subdir, name);
    png_write_rgba(path, wdt, hgt, rgba);
    free(rgba);
}

/* Up to four 320x256 (PAL) / 320x240 (NTSC) 24-bit RGB screens shown at boot (FUN_80020990 switches the display to isrgb24). */
void rip_splash(wad_t *w) {
    int i, n, hgt = wad_is_ntsc(w) ? 240 : 256;
    char name[64], path[PATH_MAX_LEN];
    printf("Ripping splash screens...\n");
    for (i = 0; i < 4; ++i) {
        uint32_t off = wad_hdr32(w, WAD_SPLASH_TABLE + i * 4);
        uint8_t *raw, *rgba;
        if (!off) continue;
        raw = wad_read_alloc(w, off, 320 * hgt * 3);
        rgba = xmalloc(320 * hgt * 4);
        for (n = 0; n < 320 * hgt; ++n) {
            rgba[n * 4] = raw[n * 3]; rgba[n * 4 + 1] = raw[n * 3 + 1]; rgba[n * 4 + 2] = raw[n * 3 + 2]; rgba[n * 4 + 3] = 255;
        }
        snprintf(name, sizeof(name), "Splash %d.png", i + 1);
        out_path(path, sizeof(path), "Splash", name);
        png_write_rgba(path, 320, hgt, rgba);
        free(raw); free(rgba);
    }
}

/* The demo disc has no splash screens; instead a WARNING.IN file next to WAD.IN holds one 320x256 16-bit screen. */
void rip_warning_screen(wad_t *w) {
    char dir[PATH_MAX_LEN], path[PATH_MAX_LEN];
    FILE *fp;
    uint8_t *raw;
    path_dirname(dir, sizeof(dir), w->path);
    path_join(path, sizeof(path), dir, "WARNING.IN");
    fp = fopen(path, "rb");
    if (!fp) return;
    printf("Ripping WARNING.IN...\n");
    raw = xmalloc(320 * 256 * 2);
    memset(raw, 0, 320 * 256 * 2);
    if (fread(raw, 1, 320 * 256 * 2, fp) > 0) save_mem16(raw, 320, 256, "Splash", "Warning screen.png", 0);
    fclose(fp);
    free(raw);
}

/* 320x32 16-bit hint text strips shown on the loading screen (one per level). */
void rip_banners(wad_t *w) {
    int level;
    char name[64];
    printf("Ripping loading screen hints...\n");
    for (level = 1; level <= WAD_MAX_LEVEL; ++level) {
        uint32_t off = wad_hdr32(w, WAD_BANNER_TABLE + level * 4);
        if (!off || off == 0xFFFFFFFF) continue;
        snprintf(name, sizeof(name), "Level %02d hint.png", level);
        save_raw16(w, off, 320, wad_is_jp(w) ? 64 : 32, "Loading screen hints", name, 1);
    }
}

static int is_text_byte(uint8_t c) { return c >= 0x20 && c < 0x7f; }

/* The menu code addresses its strings through a pointer table stored after the title block's sound data
 * (FUN_8001fb84: count at +0x100, pointers at +0x150 past the count array). The table also names every mission. */
/* Menu strings are drawn with the 12x14 menu font, whose lowercase slots hold symbols (a/b: arrows, c: text
 * cursor, f: '?', g: '.') and, in the German build, the umlauts (k l m n = Ä Ü Ö ß). Mixed-case strings use
 * the 8x8 font instead; there the French build keeps 'é' in the 'y' slot. */
static void put_menu_string(FILE *fp, const uint8_t *s, size_t n, const wad_t *w) {
    size_t k;
    int uppercase = 1;
    for (k = 0; k < n; ++k)
        if (s[k] >= 'a' && s[k] <= 'z' && !strchr("abcfg", s[k]) && !(wad_is_german(w) && strchr("klmn", s[k]))) uppercase = 0;
    for (k = 0; k < n; ++k) {
        uint8_t c = s[k];
        const char *rep = NULL;
        if (uppercase) {
            if (wad_is_german(w)) rep = c == 'k' ? "\xC3\x84" : c == 'l' ? "\xC3\x9C" : c == 'm' ? "\xC3\x96" : c == 'n' ? "\xC3\x9F" : NULL;
            if (!rep) rep = c == 'a' ? "\xE2\x97\x84" : c == 'b' ? "\xE2\x96\xBA" : c == 'c' ? "_" : c == 'f' ? "?" : c == 'g' ? "." : NULL;
        } else if (wad_is_french(w) && c == 'y') rep = "\xC3\xA9";
        if (rep) fputs(rep, fp);
        else if (is_text_byte(c)) fputc(c, fp);
        else fprintf(fp, "\\x%02x", c);
    }
}

static void rip_title_strings(const wad_t *w, const uint8_t *blk, uint32_t snd_data, int variant) {
    uint32_t sfx = rd32(blk + snd_data - 0x88), base = snd_data + sfx, cnt, tbl, i, found = 0;
    char path[PATH_MAX_LEN], name[64];
    FILE *fp;
    if (base + 0x104 > TITLE_BLOCK_SIZE) return;
    snprintf(name, sizeof(name), "menu strings (title variant %d).txt", variant + 1);
    out_path(path, sizeof(path), "Text", name);
    fp = xfopen(path, "w");
    fprintf(fp, "# Menu strings of title block %d. Symbol glyphs decoded: arrows, '_' text cursor, '?' and '.'.\n", variant + 1);
    cnt = rd32(blk + base + 0x100);
    tbl = base + 0x104 + cnt * 4 + 0x150;
    if (cnt <= 4096 && tbl < TITLE_BLOCK_SIZE) {
        for (i = 0; tbl + (i + 1) * 4 <= TITLE_BLOCK_SIZE && i < 1024; ++i) {
            uint32_t o = rd32(blk + tbl + i * 4), e;
            if (o >= TITLE_BLOCK_SIZE || (o == 0 && found)) break;
            if (o < 0x1000) continue;                      /* leading entries belong to another table */
            if (!is_text_byte(blk[o]) && blk[o] != 0) break;
            for (e = o; e < TITLE_BLOCK_SIZE && blk[e]; ++e) ;
            fprintf(fp, "%u: ", i);
            put_menu_string(fp, blk + o, e - o, w);
            fputc('\n', fp);
            ++found;
        }
    }
    if (!found) {
        /* older builds (US demo) lay the table out differently: fall back to scanning for strings */
        uint32_t o = base;
        fprintf(fp, "# (no pointer table found; strings scanned from the block)\n");
        while (o < TITLE_BLOCK_SIZE) {
            uint32_t e = o;
            while (e < TITLE_BLOCK_SIZE && is_text_byte(blk[e])) ++e;
            if (e - o >= 4 && e < TITLE_BLOCK_SIZE && blk[e] == 0) { put_menu_string(fp, blk + o, e - o, w); fputc('\n', fp); }
            o = e + 1;
        }
    }
    fclose(fp);
}

/* --exe: printable strings of the game executable, with their load addresses. */
void rip_exe_strings(const char *exe_path) {
    FILE *in = fopen(exe_path, "rb"), *fp;
    uint8_t *d;
    size_t n, i;
    char path[PATH_MAX_LEN];
    if (!in) { fprintf(stderr, "Couldn't open %s\n", exe_path); return; }
    printf("Ripping executable strings from %s...\n", exe_path);
    fseek(in, 0, SEEK_END); n = (size_t)ftell(in); fseek(in, 0, SEEK_SET);
    d = xmalloc(n ? n : 1);
    n = fread(d, 1, n, in);
    fclose(in);
    out_path(path, sizeof(path), "Text", "executable strings.txt");
    fp = xfopen(path, "w");
    fprintf(fp, "# strings of %s (PS-EXE loaded at 0x80010000)\n", exe_path);
    for (i = 0x800; i < n;) {
        size_t j = i;
        while (j < n && is_text_byte(d[j])) ++j;
        if (j - i >= 4 && j < n && d[j] == 0) {
            fprintf(fp, "%08lx: ", 0x80010000ul + (unsigned long)(i - 0x800));
            fwrite(d + i, 1, j - i, fp);
            fputc('\n', fp);
        }
        i = j + 1;
    }
    fclose(fp);
    free(d);
}

/* Title screen / menu block (FUN_8001fb84), 0x92000 bytes, five variants (3 random + 2 difficulty specific). */
#define TITLE_BG_OFFSET  0x11000     /* 4 backgrounds, 320x256 8bpp (320x240 on NTSC), consecutive */
/* Everything after the backgrounds is shifted by the background size difference on NTSC (4 x 0x1400 = 0x5000);
 * the Japanese build additionally has a smaller 8x8 font so its CLUT and everything after it sit 0xBEC earlier. */
#define TITLE_IMG_64x96  (0x61000 - d)
#define TITLE_FONT_BIG   (0x64400 - d) /* 46 (47 in JP) glyphs, 12x14 8bpp (menu font, FUN_80044e48) */
#define TITLE_FONT_SMALL (0x66230 - d) /* 62 glyphs, 8x8 8bpp (not in JP) */
#define TITLE_CLUT       (0x671B0 - d - dj) /* two 256 colour palettes: row 0 backgrounds, row 1 fonts and menu sheets */
#define TITLE_CLUT_MENU  (TITLE_CLUT + 0x200)
#define TITLE_ICONS      (TITLE_CLUT + 0x400) /* 13 icons 24x18 16-bit, 0x360 bytes each */
#define TITLE_IMG_64x40  (TITLE_ICONS + 12 * 0x360 + 0x360)
#define TITLE_IMG_64x192 (TITLE_ICONS + 12 * 0x360 + 0x1760)
#define TITLE_SND_SIZE   (TITLE_ICONS + 12 * 0x360 + 0x7760) /* size of the menu sound effects (uploaded to SPU) */
#define TITLE_MUSIC_SIZE (TITLE_SND_SIZE + 4) /* size of the menu music, streamed from WAD_TITLE_SND_TABLE[i] */
#define TITLE_SND_TABLE  (TITLE_SND_SIZE + 8) /* 8 entries of 16 bytes */
#define TITLE_SND_DATA   (TITLE_SND_TABLE + 0x80)

void rip_title_text(wad_t *w);

void rip_title(wad_t *w) {
    int i, k;
    /* dj: shift of the palette and everything after the fonts: Japanese has no 8x8 font (-0xBEC),
     * German has four extra 12x14 glyphs for umlauts (+0x2A0, i.e. dj = -0x2A0) */
    const int bg_h = wad_is_ntsc(w) ? 240 : 256, bg_bytes = 320 * bg_h, d = wad_is_ntsc(w) ? 0x5000 : 0,
              dj = wad_is_jp(w) ? 0xBEC : wad_is_german(w) ? -0x2A0 : wad_is_french(w) ? -0xA8 : 0;
    const int nglyphs = wad_is_jp(w) ? 47 : wad_is_german(w) ? 50 : wad_is_french(w) ? 47 : 46;
    char subdir[64], name[96], path[PATH_MAX_LEN];
    printf("Ripping title screens...\n");
    for (i = 0; i < wad_title_count(w); ++i) {
        uint32_t off = wad_hdr32(w, WAD_TITLE_TABLE + i * 4), snd_size;
        uint8_t *blk;
        if (!off) continue;
        blk = wad_read_alloc(w, off, TITLE_BLOCK_SIZE);
        snprintf(subdir, sizeof(subdir), "Title/Variant %d", i + 1);
        if (wad_is_usdemo(w)) save_mem16(blk, 160, 64, subdir, "Title image 160x64.png", 1);   /* loaded to VRAM (768,448) */
        for (k = 0; k < 4; ++k) {
            snprintf(name, sizeof(name), "Background %d.png", k + 1);
            out_path(path, sizeof(path), subdir, name);
            write_indexed8_png(path, blk + TITLE_BG_OFFSET + k * bg_bytes, 320, bg_h, blk + TITLE_CLUT, 0);
        }
        /* menu fonts, drawn with the title palette */
        {
            uint8_t *strip = xmalloc(62 * 12 * 14);
            int g, y;
            const uint32_t small_font = TITLE_FONT_SMALL + (nglyphs - 46) * 0xA8;
            for (g = 0; g < nglyphs; ++g)
                for (y = 0; y < 14; ++y)
                    memcpy(strip + (y * nglyphs + g) * 12, blk + TITLE_FONT_BIG + g * 0xA8 + y * 12, 12);
            out_path(path, sizeof(path), subdir, "Menu font 12x14.png");
            write_indexed8_png(path, strip, nglyphs * 12, 14, blk + TITLE_CLUT_MENU, 1);
            if (!wad_is_jp(w)) {
                for (g = 0; g < 62; ++g)
                    for (y = 0; y < 8; ++y)
                        memcpy(strip + (y * 62 + g) * 8, blk + small_font + g * 0x40 + y * 8, 8);
                out_path(path, sizeof(path), subdir, "Menu font 8x8.png");
                write_indexed8_png(path, strip, 62 * 8, 8, blk + TITLE_CLUT_MENU, 1);
            }
            free(strip);
        }
        /* 8bpp menu sheet (VRAM 320,256; drawn with CLUT row 1) and the four 16x16 icons below it */
        out_path(path, sizeof(path), subdir, "Menu sheet 128x96.png");
        write_indexed8_png(path, blk + TITLE_IMG_64x96, 128, 96, blk + TITLE_CLUT_MENU, 1);
        {
            uint8_t icon[16 * 16];
            int y;
            for (k = 0; k < 4; ++k) {
                for (y = 0; y < 16; ++y) memcpy(icon + y * 16, blk + 0x64000 - d + k * 0x100 + y * 16, 16);
                snprintf(name, sizeof(name), "Menu icon %d (16x16).png", k + 1);
                out_path(path, sizeof(path), subdir, name);
                write_indexed8_png(path, icon, 16, 16, blk + TITLE_CLUT_MENU, 1);
            }
        }
        /* the 64x192 block at VRAM (960,0) is the in-game text render target; its stored content looks like
           leftover render data ("LOADING" drawn progressively) and is kept as a 16-bit dump only */
        save_mem16(blk + TITLE_IMG_64x40, 64, 40, subdir, "Image 64x40.png", 1);
        save_mem16(blk + TITLE_IMG_64x192, 64, 192, subdir, "Image 64x192.png", 1);
        for (k = 0; k < 13; ++k) {
            snprintf(name, sizeof(name), "Icon %02d.png", k + 1);
            save_mem16(blk + TITLE_ICONS + k * 0x360, 24, 18, subdir, name, 1);
        }
        /* menu sounds: 8 descriptors (offset, ?, pitch, ?) followed by the SPU data */
        snd_size = rd32(blk + TITLE_SND_SIZE);
        if (snd_size && TITLE_SND_DATA + snd_size <= TITLE_BLOCK_SIZE) {
            for (k = 0; k < 8; ++k) {
                uint32_t so = rd32(blk + TITLE_SND_TABLE + k * 16), pitch = rd32(blk + TITLE_SND_TABLE + k * 16 + 8);
                uint32_t end = (k < 7) ? rd32(blk + TITLE_SND_TABLE + (k + 1) * 16) : snd_size;
                if (end > snd_size || so >= end) continue;
                snprintf(name, sizeof(name), "Menu sound %d.wav", k + 1);
                out_path(path, sizeof(path), subdir, name);
                adpcm_write_wav(path, blk + TITLE_SND_DATA + so, end - so, (uint32_t)((uint64_t)(pitch ? pitch : 0x800) * 44100 / 4096), 1);
            }
        }
        /* menu music: a separate ADPCM stream at WAD_TITLE_SND_TABLE[i], its size is stored in the title block */
        {
            uint32_t moff = wad_hdr32(w, wad_title_snd_table(w) + i * 4), msize = rd32(blk + TITLE_MUSIC_SIZE);
            if (moff && msize && msize < 0x400000) {
                uint8_t *m = wad_read_alloc(w, moff, msize);
                out_path(path, sizeof(path), subdir, "Menu music.wav");
                adpcm_write_wav(path, m, msize, 22050, 0);
                out_path(path, sizeof(path), subdir, "Menu music.vag");
                vag_write(path, m, msize, 22050, "Menu music");
                free(m);
            }
        }
        free(blk);
    }
}

void rip_title_text(wad_t *w) {
    int i;
    const int d = wad_is_ntsc(w) ? 0x5000 : 0, dj = wad_is_jp(w) ? 0xBEC : wad_is_german(w) ? -0x2A0 : wad_is_french(w) ? -0xA8 : 0;
    printf("Ripping menu strings...\n");
    for (i = 0; i < wad_title_count(w); ++i) {
        uint32_t off = wad_hdr32(w, WAD_TITLE_TABLE + i * 4);
        uint8_t *blk;
        if (!off) continue;
        blk = wad_read_alloc(w, off, TITLE_BLOCK_SIZE);
        rip_title_strings(w, blk, TITLE_SND_DATA, i);
        free(blk);
    }
}
