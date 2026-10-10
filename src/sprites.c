/* Enemy, weapon, psyonic and HUD sprite extraction.
 * Offsets follow the game's loaders (see FUN_80016a3c / FUN_80014998 in SLES_005.35). */
#include "rippers.h"
#include "png.h"
#include "psx.h"
#include "platform.h"

#define ENEMY_SHEET_W 128
#define ENEMY_SHEET_H 192
#define ENEMY_SHEET_BYTES 0x6000

#define WEAP_SHEET_OFFSET  0x708
#define WEAP_SHEET_W       384
#define WEAP_SHEET_H       96
#define WEAP_PAL0_OFFSET   0x88
#define WEAP_PAL1_OFFSET   0x488
#define WEAP_DATA_BASE     0x11000
#define NUM_WEAPONS        10

#define PSY_BLOCK_SIZE     0x11800
#define PSY_PAL_OFFSET     0x64
#define PSY_DATA_OFFSET    0x864
#define PSY_SHEET_W        512
#define PSY_SHEET_H        64
#define NUM_PSYONICS       6

#define HUD_SHEET_W        256
#define HUD_SHEET_H        96

void write_indexed8_png(const char *path, const uint8_t *idx, int w, int h, const uint8_t *pal, int transparent0) {
    uint8_t *rgba = xmalloc((size_t)w * h * 4);
    int i;
    psx_indexed8_to_rgba(idx, w, h, pal, rgba);
    for (i = 0; i < w * h; ++i) rgba[i * 4 + 3] = (idx[i] == 0 && transparent0) ? 0 : (transparent0 ? rgba[i * 4 + 3] : 255);
    png_write_rgba(path, w, h, rgba);
    free(rgba);
}

void write_indexed4_png(const char *path, const uint8_t *idx, int w, int h, const uint8_t *pal) {
    uint8_t *rgba = xmalloc((size_t)w * h * 4);
    psx_indexed4_to_rgba(idx, w, h, pal, rgba);
    png_write_rgba(path, w, h, rgba);
    free(rgba);
}

/* Port of the game's sprite decompressor: the frame is a stream of skip/copy commands
 * referencing three side streams (24-bit runs, 16-bit pairs, single bytes) plus raw dwords. */
void sprite_decode(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len) {
    const uint8_t *end = src + src_len, *a2, *t0, *a3, *a1, *p;
    uint8_t *d = dst, *dend = dst + dst_len;
    uint32_t o2, o3, o1;

    memset(dst, 0, dst_len);
    if (src_len < 12) return;
    o2 = rd32(src); o3 = rd32(src + 4); o1 = rd32(src + 8);
    p = src + 12;
    a2 = p + o2;            /* 24-bit (3 byte) entries stored as dwords */
    t0 = a2 + 3;            /* single bytes, stride 4 */
    a3 = a2 + o3;           /* 16-bit entries */
    a1 = a3 + o1;           /* raw dword runs */

#define NEED(ptr, n) if ((ptr) + (n) > end) return;
#define OUT(n) if (d + (n) > dend) return;
    for (;;) {
        int t2, t4;
        NEED(p, 1);
        t2 = *p++;
        if (t2 == 0xFF) break;
        if (t2 & 0x80) { d += t2 & 0x7F; continue; }
        d += t2;
        switch (t2 & 3) {
        case 0: break;
        case 1: NEED(a2, 4); OUT(3); d[0] = a2[0]; d[1] = a2[1]; d[2] = a2[2]; a2 += 4; d += 3; break;
        case 2: NEED(a3, 2); OUT(2); d[0] = a3[0]; d[1] = a3[1]; a3 += 2; d += 2; break;
        case 3: NEED(t0, 1); OUT(1); *d++ = *t0; t0 += 4; break;
        }
        NEED(p, 1);
        t2 = *p++;
        t4 = (t2 & 0x3F) * 4;
        NEED(a1, (size_t)t4); OUT((size_t)t4);
        memcpy(d, a1, (size_t)t4);
        d += t4; a1 += t4;
        switch (t2 >> 6) {
        case 0: break;
        case 1: NEED(t0, 1); OUT(1); *d = *t0; d += 4; t0 += 4; break;
        case 2: NEED(a3, 2); OUT(2); d[0] = a3[0]; d[1] = a3[1]; d += 4; a3 += 2; break;
        case 3: NEED(a2, 4); OUT(3); d[0] = a2[0]; d[1] = a2[1]; d[2] = a2[2]; d += 4; a2 += 4; break;
        }
    }
#undef NEED
#undef OUT
}

/* ---------------------------------------------------------------- enemies */

/* Cut the individual sprites out of a set's 64x192 word sprite sheet. The set's object type table
 * (0x38 byte entries, count at +0x114) points to 16 byte sprite entries:
 *   +4 x (8bpp texels, or half the 4bpp texel x), +5 y, +8 width, +9 height,
 *   +0xd 1 = 8bpp using palette +0xf, else 4bpp with its 16 colours stored in the sheet at row +0xf, word +0xe. */
static void rip_piece_sheet_sprites(const uint8_t *blk, size_t blk_len, uint32_t sheet_off, uint32_t pal_off, const char *subdir, int set) {
    uint32_t anim = rd32(blk + 4), ntypes = rd32(blk + 0x114), k;
    const uint8_t *sheet = blk + sheet_off;
    uint8_t *idx = xmalloc(256 * 192);
    char name[128], path[PATH_MAX_LEN];
    if (ntypes > 16 || anim + ntypes * 0x38 > blk_len) { free(idx); return; }
    for (k = 0; k < ntypes; ++k) {
        const uint8_t *t = blk + anim + k * 0x38;
        uint32_t sub = rd32(t), start = t[0x2c], cnt = t[0x27], j;
        if (sub + (start + cnt) * 16 > blk_len) continue;
        for (j = start; j < start + cnt; ++j) {
            const uint8_t *e = blk + sub + j * 16;
            unsigned x = e[4], y = e[5], wdt = e[8], hgt = e[9], r, cc;
            const uint8_t *pal;
            if (!wdt || !hgt || y + hgt > ENEMY_SHEET_H) continue;
            if (e[0xd] == 1) {
                if (x + wdt > ENEMY_SHEET_W) continue;
                for (r = 0; r < hgt; ++r) memcpy(idx + r * wdt, sheet + (y + r) * ENEMY_SHEET_W + x, wdt);
                pal = blk + pal_off + (e[0xf] & 1) * 0x200;
            } else {
                unsigned u = x * 2;
                if (u + wdt > ENEMY_SHEET_W * 2 || e[0xf] >= ENEMY_SHEET_H || e[0xe] + 16 > 64) continue;
                for (r = 0; r < hgt; ++r)
                    for (cc = 0; cc < wdt; ++cc) {
                        uint8_t b = sheet[(y + r) * ENEMY_SHEET_W + (u + cc) / 2];
                        idx[r * wdt + cc] = ((u + cc) & 1) ? (b >> 4) : (b & 15);
                    }
                pal = sheet + e[0xf] * ENEMY_SHEET_W + e[0xe] * 2;
            }
            snprintf(name, sizeof(name), "Set %02d - Type %u - Sprite %02u.png", set, k + 1, j - start + 1);
            out_path(path, sizeof(path), subdir, name);
            write_indexed8_png(path, idx, wdt, hgt, pal, 1);
        }
    }
    free(idx);
}

typedef struct piece_sig_s { uint32_t size, nspr, pal; } piece_sig_t;

static void rip_piece(const uint8_t *blk, size_t blk_len, const char *subdir, int set) {
    uint32_t desc_off = rd32(blk), pal_off = rd32(blk + 0x100), emb_off = rd32(blk + 0x108);
    uint32_t emb_len = rd32(blk + 0x10C), sheet_off = rd32(blk + 0x110);
    unsigned nspr = blk[0x325], i;
    uint8_t *frame = xmalloc(256 * 256);
    char name[128], path[PATH_MAX_LEN];

    if (pal_off + 0x400 > blk_len || sheet_off + ENEMY_SHEET_BYTES > blk_len || desc_off + nspr * 20 > blk_len) {
        fprintf(stderr, "  skipping malformed sprite set %d in %s\n", set, subdir);
        free(frame);
        return;
    }
    for (i = 0; i < nspr; ++i) {
        const uint8_t *d = blk + desc_off + i * 20;
        uint32_t off = rd32(d);
        unsigned pal = d[6] & 1, wdt = d[8], hgt = d[9];
        if (off >= blk_len || wdt == 0 || hgt == 0) continue;
        sprite_decode(blk + off, blk_len - off, frame, (size_t)wdt * hgt);
        snprintf(name, sizeof(name), "Set %02d - Frame %03d.png", set, i + 1);
        out_path(path, sizeof(path), subdir, name);
        write_indexed8_png(path, frame, wdt, hgt, blk + pal_off + pal * 0x200, 1);
    }
    for (i = 0; i < 2; ++i) {
        snprintf(name, sizeof(name), "Set %02d - Sheet (palette %u).png", set, i + 1);
        out_path(path, sizeof(path), subdir, name);
        write_indexed8_png(path, blk + sheet_off, ENEMY_SHEET_W, ENEMY_SHEET_H, blk + pal_off + i * 0x200, 1);
    }
    for (i = 0; i < emb_len / 0x20 && emb_off + (i + 1) * 0x20 <= blk_len; ++i) {
        snprintf(name, sizeof(name), "Set %02d - Sheet 4bpp (embedded palette %u).png", set, i + 1);
        out_path(path, sizeof(path), subdir, name);
        write_indexed4_png(path, blk + sheet_off, ENEMY_SHEET_W * 2, ENEMY_SHEET_H, blk + emb_off + i * 0x20);
    }
    rip_piece_sheet_sprites(blk, blk_len, sheet_off, pal_off, subdir, set);
    /* frame list and object type records, so animation sequences can be reconstructed */
    {
        uint32_t anim = rd32(blk + 4), ntypes = rd32(blk + 0x114), k;
        FILE *fp;
        snprintf(name, sizeof(name), "Set %02d - frames.txt", set);
        out_path(path, sizeof(path), subdir, name);
        fp = xfopen(path, "w");
        fprintf(fp, "# RLE frames in table order (the order is the animation sequence; repeated entries mark loops).\n"
                    "# From the AI code (FUN_800267f0): duration = frames the sprite is shown, action = per-frame behaviour code\n"
                    "# (0/1 move forward/backward, 2/3 short pause, 4/5 long pause); the entity's current frame index is byte 0x3b.\n"
                    "# frame, data offset, size, palette, duration, w, h, byte10, byte11, action, bytes 12-19\n");
        for (i = 0; i < nspr; ++i) {
            const uint8_t *d = blk + desc_off + i * 20;
            fprintf(fp, "%3u, 0x%05X, %u, %u, %u, %u, %u, %u, %u, %u, %02x%02x%02x%02x %02x%02x%02x%02x\n", i + 1, rd32(d), rd16(d + 4), d[6], d[7], d[8], d[9], d[10], d[11], d[16],
                    d[12], d[13], d[14], d[15], d[16], d[17], d[18], d[19]);
        }
        fprintf(fp, "\n# object type records (0x38 bytes): +0 sheet sprite list, +0x27 sheet sprite count, +0x2c first sheet sprite,\n"
                    "# +0x2d..+0x2f and +0x30..+0x37 look like per-animation / per-direction frame indices\n");
        for (k = 0; k < ntypes && k < 16 && anim + (k + 1) * 0x38 <= blk_len; ++k) {
            uint32_t b;
            fprintf(fp, "type %u: ", k + 1);
            for (b = 0; b < 0x38; ++b) fprintf(fp, "%02x%s", blk[anim + k * 0x38 + b], (b % 4 == 3) ? " " : "");
            fputc('\n', fp);
        }
        fclose(fp);
    }
    free(frame);
}

void rip_enemy_sprites(wad_t *w) {
    int level;
    for (level = 1; level <= WAD_MAX_LEVEL; ++level) {
        uint32_t loff = wad_level_offset(w, level);
        uint8_t hdr[0x100];
        uint32_t ld_size, seen_area[16];
        int nseen = 0, a, nsets = 0;
        piece_sig_t sigs[64];
        char subdir[64];

        if (!loff) continue;
        wad_read(w, loff, hdr, sizeof(hdr));
        ld_size = rd32(hdr + 4);
        snprintf(subdir, sizeof(subdir), "Enemies/Level %02d", level);
        printf("Ripping level %d enemy sprites...\n", level);
        for (a = 0; a < 16; ++a) {
            uint32_t aoff = loff + ld_size + rd32(hdr + 8 + a * 4) + wad_level_block_size(w);
            uint32_t asize = rd32(hdr + 0x48 + a * 4);
            uint8_t ahdr[64];
            int p, dup = 0;
            if (!asize) continue;
            for (p = 0; p < nseen; ++p) if (seen_area[p] == aoff) dup = 1;
            if (dup) continue;
            seen_area[nseen++] = aoff;
            wad_read(w, aoff, ahdr, sizeof(ahdr));
            for (p = 0; p < 8; ++p) {
                uint32_t poff = aoff + rd32(ahdr + p * 4) * 0x800, psize = rd32(ahdr + 32 + p * 4);
                uint8_t *blk;
                piece_sig_t sig;
                int s, known = 0;
                if (!rd32(ahdr + p * 4) || psize < 0x400 || psize > 0x400000) continue;
                blk = wad_read_alloc(w, poff, psize);
                sig.size = psize; sig.nspr = blk[0x325]; sig.pal = rd32(blk + 0x100);
                for (s = 0; s < nsets; ++s)
                    if (sigs[s].size == sig.size && sigs[s].nspr == sig.nspr && sigs[s].pal == sig.pal) known = 1;
                if (!known && nsets < 64) {
                    sigs[nsets++] = sig;
                    rip_piece(blk, psize, subdir, nsets);
                }
                free(blk);
            }
        }
    }
}

/* ---------------------------------------------------------------- weapons */

static void write_weapon_frames(const uint8_t *blk, size_t blk_len, uint32_t desc_off, uint32_t data_off,
                                unsigned count, int desc_size, const char *subdir, const char *kind, int red_variant) {
    uint8_t *frame = xmalloc(256 * 256);
    unsigned j;
    char name[128], path[PATH_MAX_LEN];
    for (j = 0; j < count; ++j) {
        const uint8_t *d = blk + desc_off + j * desc_size;
        uint32_t off;
        unsigned pal, wdt, hgt;
        if (desc_off + (j + 1) * desc_size > blk_len) break;
        off = rd32(d); pal = d[6] & 1; wdt = d[7]; hgt = d[8];
        if (data_off + off >= blk_len || wdt == 0 || hgt == 0) break;
        sprite_decode(blk + data_off + off, blk_len - data_off - off, frame, (size_t)wdt * hgt);
        snprintf(name, sizeof(name), "%s frame %02u.png", kind, j + 1);
        out_path(path, sizeof(path), subdir, name);
        write_indexed8_png(path, frame, wdt, hgt, blk + WEAP_PAL0_OFFSET + pal * 0x200, 1);
        if (red_variant) {
            snprintf(name, sizeof(name), "%s frame %02u (red).png", kind, j + 1);
            out_path(path, sizeof(path), subdir, name);
            write_indexed8_png(path, frame, wdt, hgt, blk + WEAP_PAL0_OFFSET + 0x200, 1);
        }
    }
    free(frame);
}

void rip_weapons(wad_t *w) {
    int i;
    const size_t blk_len = 0x80000;
    printf("Ripping weapons...\n");
    for (i = 0; i < NUM_WEAPONS; ++i) {
        uint32_t boff = wad_hdr32(w, WAD_WEAPON_TABLE + (wad_first_group(w, WAD_WEAPON_TABLE, 10) * 10 + i) * 4);
        uint8_t *blk = wad_read_alloc(w, boff, blk_len);
        const uint8_t *sheet = blk + WEAP_SHEET_OFFSET;
        char subdir[64], path[PATH_MAX_LEN], name[128];
        int red = (i == 3 || i == 4), k;

        snprintf(subdir, sizeof(subdir), "Weapons/Weapon %02d", i + 1);
        for (k = 0; k < 2; ++k) {
            snprintf(name, sizeof(name), "Sheet (palette %d).png", k + 1);
            out_path(path, sizeof(path), subdir, name);
            write_indexed8_png(path, sheet, WEAP_SHEET_W, WEAP_SHEET_H, blk + WEAP_PAL0_OFFSET + k * 0x200, 1);
        }
        snprintf(name, sizeof(name), "Sheet (palette 3).png");
        out_path(path, sizeof(path), subdir, name);
        write_indexed8_png(path, sheet, WEAP_SHEET_W, WEAP_SHEET_H, blk + WEAP_PAL1_OFFSET, 1);

        write_weapon_frames(blk, blk_len, rd32(blk + 0x00) + WEAP_DATA_BASE, rd32(blk + 0x14) + WEAP_DATA_BASE,
                            rd32(blk + 0x24), 20, subdir, "Static", red);
        write_weapon_frames(blk, blk_len, rd32(blk + 0x04) + WEAP_DATA_BASE, rd32(blk + 0x18) + WEAP_DATA_BASE,
                            rd32(blk + 0x28), 12, subdir, "Dynamic", red);

        /* Sheets whose palettes are embedded in the sheet itself (4bpp regions). */
        if (i == 8) {   /* Zodiac */
            out_path(path, sizeof(path), subdir, "Sheet 4bpp (embedded palette).png");
            write_indexed4_png(path, sheet, WEAP_SHEET_W * 2, WEAP_SHEET_H, sheet + 384 * 84);
        }
        if (i == 3 || i == 4) {   /* Phase rifle / phase repeater share four 16 colour palettes */
            static const int rows[4] = {48, 60, 72, 84};
            for (k = 0; k < 4; ++k) {
                snprintf(name, sizeof(name), "Sheet 4bpp (embedded palette %d).png", k + 1);
                out_path(path, sizeof(path), subdir, name);
                write_indexed4_png(path, sheet, WEAP_SHEET_W * 2, WEAP_SHEET_H, sheet + 384 * rows[k] + 256);
            }
        }
        free(blk);
    }
}

/* ---------------------------------------------------------------- psyonics */

void rip_psyonics(wad_t *w) {
    int i, k;
    printf("Ripping psyonics...\n");
    for (i = 0; i < NUM_PSYONICS; ++i) {
        uint32_t boff = wad_hdr32(w, WAD_PSYONIC_TABLE + (wad_first_group(w, WAD_PSYONIC_TABLE, 10) * 10 + i) * 4);
        uint8_t *blk = wad_read_alloc(w, boff, PSY_BLOCK_SIZE);
        char path[PATH_MAX_LEN], name[128];
        for (k = 0; k < 2; ++k) {
            snprintf(name, sizeof(name), "Psyonic %d - palette %d.png", i + 1, k + 1);
            out_path(path, sizeof(path), "Psyonics", name);
            write_indexed8_png(path, blk + PSY_DATA_OFFSET, PSY_SHEET_W, PSY_SHEET_H, blk + PSY_PAL_OFFSET + k * 0x200, 1);
        }
        free(blk);
    }
}

/* ---------------------------------------------------------------- HUD */

void rip_hud(wad_t *w) {
    uint32_t hoff = wad_hdr32(w, WAD_HUD_OFFSET), hsize = wad_hdr32(w, WAD_HUD_OFFSET + 4);
    uint8_t *blk;
    int k;
    char path[PATH_MAX_LEN], name[128];
    printf("Ripping HUD...\n");
    if (hsize < 0x1400 + HUD_SHEET_W * HUD_SHEET_H) hsize = 0x1400 + HUD_SHEET_W * HUD_SHEET_H;
    blk = wad_read_alloc(w, hoff, hsize);
    /* +0x800: six 256 colour palettes (loaded at VRAM 512,175), +0x1400: 256x96 8bpp sheet (896,416) */
    for (k = 0; k < 6; ++k) {
        snprintf(name, sizeof(name), "HUD - palette %d.png", k + 1);
        out_path(path, sizeof(path), "HUD", name);
        write_indexed8_png(path, blk + 0x1400, HUD_SHEET_W, HUD_SHEET_H, blk + 0x800 + k * 0x200, 1);
    }
    /* 66 sprite descriptors (8 bytes: s16 abr, s16 palette row, u8 x, u8 y, u8 w, u8 h) at +0x20 + table[0] */
    {
        uint32_t tbl = 0x20 + rd32(blk);
        uint8_t *idx = xmalloc(HUD_SHEET_W * HUD_SHEET_H);
        for (k = 0; k < 66 && tbl + (k + 1) * 8 <= hsize; ++k) {
            const uint8_t *e = blk + tbl + k * 8;
            unsigned pal = rd16(e + 2) & 7, x = e[4], y = e[5], wdt = e[6], hgt = e[7], r;
            if (!wdt || !hgt || x + wdt > HUD_SHEET_W || y + hgt > HUD_SHEET_H || pal > 5) continue;
            for (r = 0; r < hgt; ++r) memcpy(idx + r * wdt, blk + 0x1400 + (y + r) * HUD_SHEET_W + x, wdt);
            snprintf(name, sizeof(name), "hud_%02d.png", k);
            out_path(path, sizeof(path), "HUD/sprites", name);
            write_indexed8_png(path, idx, wdt, hgt, blk + 0x800 + pal * 0x200, 1);
        }
        free(idx);
    }
    free(blk);
}
