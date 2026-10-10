/* Level textures, VRAM snapshots and world geometry.
 *
 * Level block layout (FUN_800153b4):
 *   +0x0000  0x800 byte header: [0] offset of SPU data in the level data, [1] level data size,
 *            [2..17] 16 area offsets, [18..33] 16 area sizes, then level parameters and, from
 *            dword 70 on, the sizes/counts of the tables inside the level data.
 *   +0x0800  64x192 16-bit image loaded to VRAM (896,0)      (8bpp sprite/texture page)
 *   +0x6800  704x320 16-bit image loaded to VRAM (320,192)   (CLUTs + texture pages)
 *   +0x74800 level data; it begins with dword70 texture entries of 8 bytes:
 *            u8 u0, u8 v0, u16 clut, u8 u1, u8 v1, u16 tpage  -> a 32x32 texel texture
 *   Areas follow the level data. An area holds up to 64 "objects" (pointer table at +0x98),
 *   each with a chain of meshes at +0x10 and an origin at +0x18/+0x1c/+0x20 (world = origin*8 + vertex).
 * Mesh (FUN_800463a0): u32 hdr (low byte: visibility id, high 24 bits: byte size), u32 vertex offset,
 *   per-vertex colour bytes, a 15-bit colour table growing backwards from the vertex array,
 *   packed vertices (x bits 20-29, y bits 10-19, z bits 0-9, all *16; bits 30-31: extra copies)
 *   terminated by 0xFFFFFFFF, then quads of two u32 (A bits 22-31, B bits 12-21, texture (w0 & 0xffc)>>2 /
 *   C bits 22-31, D bits 12-21, flags bits 0-11) terminated by 0xFFFFFFFF. */
#include "rippers.h"
#include "png.h"
#include "psx.h"
#include "platform.h"

#define TEX_SIZE 32

typedef struct level_s {
    int num;
    int jp;               /* Japanese release: different text encoding and font tables */
    int usdemo;           /* US demo: shorter level header tail */
    int german;           /* German release: umlauts encoded in the font's lowercase slots */
    int french;           /* French release: hyphen/apostrophe in the w/x slots */
    uint32_t off;
    uint8_t *lb;          /* 0x74800 header+images */
    uint8_t *ld;          /* level data */
    uint32_t ld_size;
    uint32_t ntex;
    psx_vram_t *vram;
} level_t;

static int level_load(wad_t *w, int num, level_t *lv) {
    memset(lv, 0, sizeof(*lv));
    lv->num = num;
    lv->jp = wad_is_jp(w);
    lv->usdemo = wad_is_usdemo(w);
    lv->german = wad_is_german(w);
    lv->french = wad_is_french(w);
    lv->off = wad_level_offset(w, num);
    if (!lv->off) return 0;
    lv->lb = wad_read_alloc(w, lv->off, wad_level_block_size(w));
    lv->ld_size = rd32(lv->lb + 4);
    if (lv->ld_size > 0x400000) lv->ld_size = 0x400000;
    lv->ld = wad_read_alloc(w, lv->off + wad_level_block_size(w), lv->ld_size);
    lv->ntex = rd32(lv->lb + 70 * 4);
    if (lv->ntex * 8 > lv->ld_size) lv->ntex = lv->ld_size / 8;
    lv->vram = xcalloc(1, sizeof(psx_vram_t));
    if (wad_is_ntsc(w)) {
        psx_vram_load(lv->vram, 0x000, 0x1E0, 0x140, 0x20, lv->lb + 0x800);    /* loading hint strip */
        psx_vram_load(lv->vram, 0x140, 0xC0, 0x2C0, 0x140, lv->lb + 0x5800);
    } else {
        psx_vram_load(lv->vram, 0x380, 0x00, 0x40, 0xC0, lv->lb + 0x800);
        psx_vram_load(lv->vram, 0x140, 0xC0, 0x2C0, 0x140, lv->lb + 0x6800);
    }
    return 1;
}

static void level_free(level_t *lv) {
    free(lv->lb); free(lv->ld); free(lv->vram);
}

static void decode_texture(const level_t *lv, uint32_t i, uint8_t *rgba) {
    const uint8_t *e = lv->ld + i * 8;
    psx_vram_decode(lv->vram, rd16(e + 6), rd16(e + 2), e[0], e[1], TEX_SIZE, TEX_SIZE, rgba);
}

static void rip_level_textures(const level_t *lv, const char *subdir) {
    char texdir[160], path[PATH_MAX_LEN], name[64];
    uint8_t *rgba = xmalloc(TEX_SIZE * TEX_SIZE * 4), *atlas, *vram;
    uint32_t i, cols = 16, rows = (lv->ntex + cols - 1) / cols;

    snprintf(texdir, sizeof(texdir), "%s/textures", subdir);
    atlas = xcalloc((size_t)cols * TEX_SIZE * (rows ? rows : 1) * TEX_SIZE, 4);
    for (i = 0; i < lv->ntex; ++i) {
        int y;
        decode_texture(lv, i, rgba);
        snprintf(name, sizeof(name), "tex_%03u.png", i);
        out_path(path, sizeof(path), texdir, name);
        png_write_rgba(path, TEX_SIZE, TEX_SIZE, rgba);
        for (y = 0; y < TEX_SIZE; ++y)
            memcpy(atlas + (((size_t)(i / cols) * TEX_SIZE + y) * cols * TEX_SIZE + (i % cols) * TEX_SIZE) * 4,
                   rgba + y * TEX_SIZE * 4, TEX_SIZE * 4);
    }
    if (lv->ntex) {
        out_path(path, sizeof(path), subdir, "texture_atlas.png");
        png_write_rgba(path, cols * TEX_SIZE, rows * TEX_SIZE, atlas);
    }
    vram = xmalloc(1024 * 512 * 4);
    psx_vram_to_rgba(lv->vram, vram);
    out_path(path, sizeof(path), subdir, "vram.png");
    png_write_rgba(path, 1024, 512, vram);
    free(vram); free(atlas); free(rgba);
}

/* ---------------------------------------------------------------- geometry */

typedef struct objwriter_s {
    FILE *obj;
    long vbase;           /* vertices written so far */
    uint8_t *tex_used;    /* ntex flags */
    uint32_t ntex;
    int cur_tex;
} objwriter_t;

static void vertex_color(const uint8_t *base, uint32_t voff, uint8_t cb, uint8_t *rgb) {
    if (cb < 0x80) { rgb[0] = rgb[1] = rgb[2] = (uint8_t)(cb * 2); return; }
    if ((uint32_t)(cb - 0x80) * 2 + 2 > voff) { rgb[0] = rgb[1] = rgb[2] = 128; return; }   /* colour table would precede the mesh */
    {
        uint16_t c = rd16(base + voff - (cb - 0x80) * 2 - 2);
        rgb[0] = (uint8_t)((c & 0x1f) * 8); rgb[1] = (uint8_t)(((c >> 5) & 0x1f) * 8); rgb[2] = (uint8_t)(((c >> 10) & 0x1f) * 8);
    }
}

/* Returns number of polygons written, or -1 if the mesh is malformed. */
static int write_mesh(objwriter_t *ow, const uint8_t *area, size_t area_len, uint32_t m, int32_t ox, int32_t oy, int32_t oz) {
    uint32_t voff, p, nexp = 0, npoly = 0;
    const uint8_t *base;
    uint32_t *src_of;      /* expanded vertex index -> source vertex index */
    uint32_t cap = 4096;

    if (m + 8 > area_len) return -1;
    voff = rd32(area + m + 4);
    base = area + m + 8;
    if (m + 8 + voff > area_len) return -1;

    /* pass 1: count vertices (with duplicates) */
    p = m + 8 + voff;
    src_of = xmalloc(cap * sizeof(uint32_t));
    {
        uint32_t src = 0, cbyte = 0;
        for (;;) {
            uint32_t v, dup, k;
            if (p + 4 > area_len) { free(src_of); return -1; }
            v = rd32(area + p); p += 4;
            if (v == 0xFFFFFFFF) break;
            dup = (v >> 30) + 1;
            for (k = 0; k < dup; ++k) {
                uint8_t rgb[3];
                if (nexp >= cap) { cap *= 2; src_of = realloc(src_of, cap * sizeof(uint32_t)); if (!src_of) die("out of memory"); }
                src_of[nexp++] = src;
                if (m + 8 + cbyte < area_len && cbyte < voff) vertex_color(base, voff, base[cbyte], rgb);
                else rgb[0] = rgb[1] = rgb[2] = 128;
                ++cbyte;
                /* world = origin*8 + vertex; export right-handed, Y up, 16 units = 1 */
                fprintf(ow->obj, "v %.4f %.4f %.4f %.3f %.3f %.3f\n",
                        (ox * 8 + (int)(((v >> 20) & 0x3ff) * 16)) / 16.0,
                        (oz * 8 + (int)((v & 0x3ff) * 16)) / 16.0,
                        -(oy * 8 + (int)(((v >> 10) & 0x3ff) * 16)) / 16.0,
                        rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
            }
            ++src;
        }
    }
    /* pass 2: polygons */
    for (;;) {
        uint32_t w0, w1, a, b, c, d, tex;
        if (p + 8 > area_len) break;
        w0 = rd32(area + p); w1 = rd32(area + p + 4); p += 8;
        if (w0 == 0xFFFFFFFF) break;
        a = w0 >> 22; b = (w0 >> 12) & 0x3ff; c = w1 >> 22; d = (w1 >> 12) & 0x3ff;
        tex = (w0 & 0xffc) >> 2;
        if (a >= nexp || b >= nexp || c >= nexp || d >= nexp) continue;
        if (tex >= ow->ntex) tex = 0;
        if ((int)tex != ow->cur_tex) {
            fprintf(ow->obj, "usemtl tex_%03u\n", tex);
            ow->cur_tex = (int)tex;
            ow->tex_used[tex] = 1;
        }
        /* screen order is A,B,D,C; A=(u0,v0) B=(u1,v0) C=(u1,v1) D=(u0,v1) */
        if (c == d)
            fprintf(ow->obj, "f %ld/1 %ld/2 %ld/3\n", ow->vbase + a + 1, ow->vbase + b + 1, ow->vbase + c + 1);
        else if (a == b)
            fprintf(ow->obj, "f %ld/1 %ld/3 %ld/4\n", ow->vbase + a + 1, ow->vbase + c + 1, ow->vbase + d + 1);
        else
            fprintf(ow->obj, "f %ld/1 %ld/2 %ld/3 %ld/4\n", ow->vbase + a + 1, ow->vbase + b + 1, ow->vbase + c + 1, ow->vbase + d + 1);
        ++npoly;
    }
    ow->vbase += nexp;
    free(src_of);
    return (int)npoly;
}

static void rip_area_geometry(objwriter_t *ow, const uint8_t *area, size_t area_len, int anum) {
    int oi;
    for (oi = 0; oi < 64; ++oi) {
        uint32_t o = rd32(area + 0x98 + oi * 4), m, guard = 0;
        int32_t ox, oy, oz;
        int mi = 0;
        if (!o || o >= area_len || area_len - o < 0x30) continue;
        m = rd32(area + o + 0x10);
        ox = rds32(area + o + 0x18); oy = rds32(area + o + 0x1c); oz = rds32(area + o + 0x20);
        fprintf(ow->obj, "o area%02d_obj%02d_id%03u\n", anum, oi, area[o + 0x28]);
        while (m && m < area_len && area_len - m >= 8 && guard++ < 256) {
            uint32_t hdr = rd32(area + m);
            int n;
            if (hdr & 0x80000000) break;
            fprintf(ow->obj, "g area%02d_obj%02d_mesh%02d_vis%02x\n", anum, oi, mi, hdr & 0xff);
            n = write_mesh(ow, area, area_len, m, ox, oy, oz);
            if (n < 0) { fprintf(stderr, "    malformed mesh at area %d object %d\n", anum, oi); break; }
            ++mi;
            if ((hdr >> 8) == 0) break;
            m += hdr >> 8;
        }
    }
}

static void rip_level_geometry(wad_t *w, const level_t *lv, const char *subdir) {
    char path[PATH_MAX_LEN], name[64];
    objwriter_t ow;
    uint32_t seen[16];
    int nseen = 0, a;

    memset(&ow, 0, sizeof(ow));
    snprintf(name, sizeof(name), "level_%02d.obj", lv->num);
    out_path(path, sizeof(path), subdir, name);
    ow.obj = xfopen(path, "w");
    ow.ntex = lv->ntex ? lv->ntex : 1;
    ow.tex_used = xcalloc(ow.ntex, 1);
    ow.cur_tex = -1;
    fprintf(ow.obj, "# Disruptor level %d, extracted by disruptor_ripper\n", lv->num);
    fprintf(ow.obj, "# units: 1 = 16 game units; +Y up; vertex colours appended to v lines\n");
    fprintf(ow.obj, "mtllib level_%02d.mtl\n", lv->num);
    fprintf(ow.obj, "vt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\n");

    for (a = 0; a < 16; ++a) {
        uint32_t aoff = lv->off + lv->ld_size + rd32(lv->lb + 8 + a * 4) + wad_level_block_size(w);
        uint32_t asize = rd32(lv->lb + 0x48 + a * 4);
        uint8_t *area;
        int k, dup = 0;
        if (!asize || asize > 0x800000) continue;
        for (k = 0; k < nseen; ++k) if (seen[k] == aoff) dup = 1;
        if (dup) continue;
        seen[nseen++] = aoff;
        area = wad_read_alloc(w, aoff, asize);
        rip_area_geometry(&ow, area, asize, a);
        free(area);
    }
    fclose(ow.obj);

    snprintf(name, sizeof(name), "level_%02d.mtl", lv->num);
    out_path(path, sizeof(path), subdir, name);
    ow.obj = xfopen(path, "w");
    {
        uint32_t i;
        for (i = 0; i < ow.ntex; ++i) {
            if (!ow.tex_used[i]) continue;
            fprintf(ow.obj, "newmtl tex_%03u\nKd 1 1 1\nmap_Kd textures/tex_%03u.png\n\n", i, i);
        }
    }
    fclose(ow.obj);
    free(ow.tex_used);
}

/* ---------------------------------------------------------------- other level data
 * Walk of the level data tables exactly as FUN_800153b4 does it; the counts/sizes are the
 * dwords from index 70 of the level header. Verified against the sound table position for every level. */
typedef struct level_tables_s {
    uint32_t sky, sky_len;                 /* sky panorama descriptor (FUN_8003b068), absent on some levels */
    uint32_t script_ctl, nscript_ctl;      /* 8 byte controllers of scripted texture animations          */
    uint32_t script_frm, nscript_frm;      /* 16 byte frames: texture entry (8 bytes) + sequencing        */
    uint32_t texanim, ntexanim, t6, nt6, pause_img, pause_len;
    uint32_t text, text_len, font_big, font_big_len, font_small, font_small_len;
    uint32_t colors, colors_len, rooms, nrooms, maprec, nmaprec, mapseg, nmapseg;
    uint32_t entities, nent, items, nitems;
    uint32_t t22, nt22, doors, ndoors, waypoints, nwaypoints, t27, nt27, t28, nt28, names, nnames, t31, nt31;
    uint32_t itemtex;
} level_tables_t;

static void level_walk(const level_t *lv, level_tables_t *t) {
    uint32_t cv[42];
    int i;
    /* The Japanese release dropped the 8x8 font table (count 9); its header is one dword shorter from there on.
     * The US demo's tail differs: after the pointer list come one value, N x 8 byte records, then the sounds. */
    for (i = 0; i < 42; ++i) {
        int src = (lv->jp && i >= 9) ? i - 1 : i;
        cv[i] = (lv->jp && i == 9) ? 0 : rd32(lv->lb + (70 + src) * 4);
    }
    if (lv->usdemo) { cv[38] = 0; }
#define C(i) cv[i]
    uint32_t p = C(0) * 8;
    t->sky = p; t->sky_len = C(1); p += C(1);
    t->script_ctl = p; t->nscript_ctl = C(2); p += C(2) * 8;
    t->script_frm = p; t->nscript_frm = C(3); p += C(3) * 16 + C(4) * 12;
    t->texanim = p; t->ntexanim = C(5); p += C(5) * 20;
    t->t6 = p; t->nt6 = C(6); p += C(6) * 16;
    t->pause_img = p; t->pause_len = C(7); p += C(7);
    t->font_big = p; t->font_big_len = C(8); p += C(8);
    t->font_small = p; t->font_small_len = C(9); p += C(9) + C(12);
    t->text = p; t->text_len = C(13); p += C(13);
    t->colors = p; t->colors_len = C(14) * 4 + C(15); p += t->colors_len + C(16) * 4;
    t->rooms = p; t->nrooms = C(16); p += C(16) * 8;
    t->maprec = p; t->nmaprec = C(17); p += C(17) * 12;
    t->mapseg = p; t->nmapseg = C(18); p += C(18) * 4;
    t->entities = p; t->nent = C(19); p += C(19) * 0x68 + 0xd80;
    t->items = p; t->nitems = C(21); p += C(21) * 16 + C(23);
    t->t22 = p; t->nt22 = C(22); p += C(22) * 12;
    t->doors = p; t->ndoors = C(24); p += C(24) * 4;
    t->waypoints = p; t->nwaypoints = C(25); p += C(25) * 12 + C(26);
    t->t27 = p; t->nt27 = C(27); p += C(27) * 36 + C(29);
    t->t28 = p; t->nt28 = C(28); p += C(28) * 16;
    t->names = p; t->nnames = C(30); p += C(30) * 4;
    t->t31 = p; t->nt31 = C(31); p += C(31) * 12;
    t->itemtex = p;
#undef C
}

static const char *level_string(const level_t *lv, const level_tables_t *t, uint32_t idx) {
    uint32_t q;
    if (idx >= t->nnames || t->names + (idx + 1) * 4 > lv->ld_size) return "?";
    q = rd32(lv->ld + t->names + idx * 4);
    if (q >= lv->ld_size || !memchr(lv->ld + q, 0, lv->ld_size - q)) return "?";
    return (const char *)lv->ld + q;
}

static void dump_table(FILE *fp, const level_t *lv, const char *title, uint32_t off, uint32_t n, uint32_t sz) {
    uint32_t i, k;
    fprintf(fp, "[%s] %u records of %u bytes at level data offset 0x%X\n", title, n, sz, off);
    for (i = 0; i < n && off + (i + 1) * sz <= lv->ld_size; ++i) {
        fprintf(fp, "%4u: ", i);
        for (k = 0; k < sz; ++k) fprintf(fp, "%02x%s", lv->ld[off + i * sz + k], (k % 4 == 3 && k + 1 < sz) ? " " : "");
        fputc('\n', fp);
    }
    fputc('\n', fp);
}

static void write_glyph_strip(const level_t *lv, uint32_t off, uint32_t len, int gw, int gh, const uint8_t *pal, const char *subdir, const char *name) {
    uint32_t ng = len / (gw * gh), g, y;
    uint8_t *strip;
    char path[PATH_MAX_LEN];
    if (!ng || off + ng * gw * gh > lv->ld_size) return;
    strip = xmalloc((size_t)ng * gw * gh);
    for (g = 0; g < ng; ++g)
        for (y = 0; y < (uint32_t)gh; ++y)
            memcpy(strip + (y * ng + g) * gw, lv->ld + off + (g * gh + y) * gw, gw);
    out_path(path, sizeof(path), subdir, name);
    write_indexed8_png(path, strip, gw * ng, gh, pal, 1);
    free(strip);
}

/* Decode a w x h 8bpp rectangle whose top-left VRAM word is (wx, wy) with the given clut id. */
static void decode_vram_rect8(const level_t *lv, int wx, int wy, uint16_t clut, int wdt, int hgt, const char *subdir, const char *name) {
    uint16_t tpage = (uint16_t)(0x80 | ((wy >> 8) << 4) | (wx >> 6));
    uint8_t *rgba = xmalloc((size_t)wdt * hgt * 4);
    char path[PATH_MAX_LEN];
    psx_vram_decode(lv->vram, tpage, clut, (wx & 63) * 2, wy & 255, wdt, hgt, rgba);
    out_path(path, sizeof(path), subdir, name);
    png_write_rgba(path, wdt, hgt, rgba);
    free(rgba);
}

/* ---------------------------------------------------------------- automap
 * The in-game map (FUN_8004109c) is vector art: 12 byte records (s16 first segment, u16 x, u16 y, u8 segment
 * count, u8 colour index, u8 discovered, u8 door index or 0xff, u8 w, u8 h) whose segments are 4 bytes
 * (x0, y0, x1, y1 relative to the record position). Coordinates are game units / 16. */
static void draw_line(uint8_t *img, int W, int H, int x0, int y0, int x1, int y1, const uint8_t *rgb) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        int e2;
        if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H) memcpy(img + ((size_t)y0 * W + x0) * 4, rgb, 3), img[((size_t)y0 * W + x0) * 4 + 3] = 255;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void rip_level_map(const level_t *lv, const level_tables_t *t, const char *subdir) {
    const int scale = 2, margin = 8;
    static const uint8_t door_rgb[3] = {255, 255, 0}, grey[3] = {160, 160, 160}, start_rgb[3] = {0, 255, 0};
    int minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1, W, H, px, py;
    uint32_t i, j;
    uint8_t *img;
    char path[PATH_MAX_LEN], name[64];
    FILE *svg;

    if (!t->nmaprec || t->maprec + t->nmaprec * 12 > lv->ld_size || t->mapseg + t->nmapseg * 4 > lv->ld_size) return;
    /* pass 1: bounds */
    for (i = 0; i < t->nmaprec; ++i) {
        const uint8_t *r = lv->ld + t->maprec + i * 12;
        int seg = rds16(r), x = rd16(r + 2), y = rd16(r + 4), n = r[6];
        for (j = 0; j < (uint32_t)n; ++j) {
            const uint8_t *b;
            if (seg < 0 || (uint32_t)(seg + j) >= t->nmapseg) break;
            b = lv->ld + t->mapseg + (seg + j) * 4;
            if (x + b[0] < minx) minx = x + b[0]; if (x + b[2] < minx) minx = x + b[2];
            if (x + b[0] > maxx) maxx = x + b[0]; if (x + b[2] > maxx) maxx = x + b[2];
            if (y + b[1] < miny) miny = y + b[1]; if (y + b[3] < miny) miny = y + b[3];
            if (y + b[1] > maxy) maxy = y + b[1]; if (y + b[3] > maxy) maxy = y + b[3];
        }
    }
    if (maxx < 0) return;
    px = (int)(rd32(lv->lb + 39 * 4) >> 4); py = (int)(rd32(lv->lb + 40 * 4) >> 4);   /* player start */
    if (px < minx) minx = px; if (px > maxx) maxx = px; if (py < miny) miny = py; if (py > maxy) maxy = py;
    W = (maxx - minx + 1) * scale + 2 * margin; H = (maxy - miny + 1) * scale + 2 * margin;
    img = xcalloc((size_t)W * H, 4);
    snprintf(name, sizeof(name), "map_%02d.svg", lv->num);
    out_path(path, sizeof(path), subdir, name);
    svg = xfopen(path, "w");
    fprintf(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" style=\"background:#000\">\n"
                 "<!-- Disruptor level %d automap; 1 unit = 8 game units (2 px per map unit), y flipped like the in-game map -->\n", W, H, W, H, lv->num);
    /* pass 2: draw (y flipped to match the game's screen orientation) */
#define SX(v) (((v) - minx) * scale + margin)
#define SY(v) ((maxy - (v)) * scale + margin)
    for (i = 0; i < t->nmaprec; ++i) {
        const uint8_t *r = lv->ld + t->maprec + i * 12;
        int seg = rds16(r), x = rd16(r + 2), y = rd16(r + 4), n = r[6], col = r[7], door = r[9];
        const uint8_t *rgb = grey;
        if (door != 0xff) rgb = door_rgb;
        else if ((uint32_t)col * 4 + 3 <= t->colors_len) rgb = lv->ld + t->colors + col * 4;
        fprintf(svg, "<g id=\"rec%u\" stroke=\"#%02x%02x%02x\" fill=\"none\">\n", i, rgb[0], rgb[1], rgb[2]);
        for (j = 0; j < (uint32_t)n; ++j) {
            const uint8_t *b;
            if (seg < 0 || (uint32_t)(seg + j) >= t->nmapseg) break;
            b = lv->ld + t->mapseg + (seg + j) * 4;
            fprintf(svg, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", SX(x + b[0]), SY(y + b[1]), SX(x + b[2]), SY(y + b[3]));
            draw_line(img, W, H, SX(x + b[0]), SY(y + b[1]), SX(x + b[2]), SY(y + b[3]), rgb);
        }
        fputs("</g>\n", svg);
    }
    fprintf(svg, "<g id=\"start\" stroke=\"#00ff00\"><line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/><line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/></g>\n</svg>\n",
            SX(px) - 6, SY(py), SX(px) + 6, SY(py), SX(px), SY(py) - 6, SX(px), SY(py) + 6);
    draw_line(img, W, H, SX(px) - 6, SY(py), SX(px) + 6, SY(py), start_rgb);
    draw_line(img, W, H, SX(px), SY(py) - 6, SX(px), SY(py) + 6, start_rgb);
#undef SX
#undef SY
    fclose(svg);
    /* opaque black background for the PNG */
    for (i = 0; i < (uint32_t)W * H; ++i) if (!img[i * 4 + 3]) img[i * 4 + 3] = 255;
    snprintf(name, sizeof(name), "map_%02d.png", lv->num);
    out_path(path, sizeof(path), subdir, name);
    png_write_rgba(path, W, H, img);
    free(img);
}

/* ---------------------------------------------------------------- sky
 * The sky is a cylindrical panorama 1280 texels around (0x500 = full turn) built from 8bpp cells.
 * Descriptor (ints): [0] rows, [1] columns, [2..] row edges, [8..] column edges, [0x20 + r*24 + c] VRAM word x
 * of cell (r,c), [0xB0 + r*24 + c] VRAM y, [0x140] clut x, [0x141] clut y (plus a pitch dependent shade offset). */
/* Flat colour drawn behind the panorama, from a table in the executable (0x80056e6c, 3 bytes per level id). */
static const uint8_t sky_bg_rgb[16][3] = {
    {0,0,0}, {0xc3,0xc1,0xb8}, {0,0,0}, {0,0,0}, {0xa5,0x81,0x95}, {0,0,0}, {0,0,0}, {0,0,0},
    {0xf2,0xf2,0xdf}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} };

static void rip_level_sky(const wad_t *w, const level_t *lv, const level_tables_t *t, const char *subdir) {
    const uint8_t *b = lv->ld + t->sky;
#define SI(i) rds32(b + (i) * 4)
    int ny, nx, r, c, x, y, W, H, cx, cy;
    uint8_t *rgba;
    char path[PATH_MAX_LEN];
    if (t->sky_len < 0x142 * 4 || t->sky + t->sky_len > lv->ld_size) return;
    ny = SI(0); nx = SI(1);
    if (ny < 1 || ny > 6 || nx < 1 || nx > 24) return;
    W = SI(8 + nx) - SI(8); H = SI(2 + ny) - SI(2);
    if (W <= 0 || H <= 0 || W > 2048 || H > 1024) return;
    cx = (SI(0x140) >> 4) * 16; cy = SI(0x141);
    rgba = xmalloc((size_t)W * H * 4);
    for (r = 0; r < ny; ++r) {
        for (c = 0; c < nx; ++c) {
            int tpx = SI(0x20 + r * 24 + c), tpy = SI(0xB0 + r * 24 + c);
            for (y = SI(2 + r); y < SI(3 + r); ++y)
                for (x = SI(8 + c); x < SI(9 + c); ++x) {
                    int u = x - SI(8 + c), v = y - SI(2 + r);
                    uint16_t word = lv->vram->px[(tpy + v) & 511][(tpx + u / 2) & 1023];
                    uint16_t col = lv->vram->px[cy & 511][(cx + ((word >> (8 * (u & 1))) & 255)) & 1023];
                    psx_color_to_rgba(col, rgba + (((size_t)(y - SI(2)) * W) + (x - SI(8))) * 4, 0);
                }
        }
    }
    out_path(path, sizeof(path), subdir, "sky.png");
    png_write_rgba(path, W, H, rgba);
    free(rgba);
    out_path(path, sizeof(path), subdir, "sky.txt");
    {
        FILE *fp = xfopen(path, "w");
        fprintf(fp, "sky panorama: %d x %d texels, %d columns x %d rows of cells; 1280 texels = one full turn\n", W, H, nx, ny);
        if (wad_is_ntsc(w)) fprintf(fp, "background colour behind the panorama: none (the NTSC build does not draw one)\n");
        else fprintf(fp, "background colour behind the panorama: #%02x%02x%02x\n",
                     sky_bg_rgb[lv->num & 15][0], sky_bg_rgb[lv->num & 15][1], sky_bg_rgb[lv->num & 15][2]);
        fclose(fp);
    }
#undef SI
}

/* Write one objective text character, decoding the localised placeholder slots. */
static void put_text_char(const level_t *lv, char ch, FILE *fp) {
    const char *rep = NULL;
    if (ch == '#') { fputc('\n', fp); return; }
    if (lv->german)
        rep = ch == 'a' ? "\xC3\x84" : ch == 'o' ? "\xC3\x96" : ch == 'u' ? "\xC3\x9C" : ch == 'l' ? "\xC3\x9F"
            : ch == '$' ? "," : ch == '&' ? "." : ch == '%' ? "!" : NULL;
    else if (lv->french)
        rep = ch == 'w' ? "-" : ch == 'x' ? "'" : ch == '&' ? "." : ch == '%' ? "!" : NULL;
    if (rep) fputs(rep, fp); else fputc(ch, fp);
}

/* --text: every objective text and item name table, collected into Text/ */
void rip_text_levels(wad_t *w) {
    char path[PATH_MAX_LEN];
    FILE *fo, *fn;
    int num;
    printf("Ripping level texts...\n");
    out_path(path, sizeof(path), "Text", "objectives.txt");
    fo = xfopen(path, "w");
    out_path(path, sizeof(path), "Text", "item names.txt");
    fn = xfopen(path, "w");
    for (num = 1; num <= WAD_MAX_LEVEL; ++num) {
        level_t lv;
        level_tables_t t;
        uint32_t i;
        if (!level_load(w, num, &lv)) continue;
        level_walk(&lv, &t);
        fprintf(fo, "== Level %02d ==\n", num);
        if (lv.jp) fprintf(fo, "(Japanese text is stored as glyph indices; see Levels/Level %02d/objective.png)\n", num);
        else if (t.text + t.text_len <= lv.ld_size)
            for (i = 0; i < t.text_len && lv.ld[t.text + i]; ++i) put_text_char(&lv, (char)lv.ld[t.text + i], fo);
        fprintf(fo, "\n\n");
        fprintf(fn, "== Level %02d ==\n", num);
        for (i = 0; i < t.nnames; ++i) fprintf(fn, "%u: %s\n", i, level_string(&lv, &t, i));
        fputc('\n', fn);
        level_free(&lv);
    }
    fclose(fo);
    fclose(fn);
}

static void rip_level_extras(wad_t *w, const level_t *lv, const char *subdir) {
    level_tables_t t;
    char path[PATH_MAX_LEN], name[96];
    uint8_t hudpal[0x200];
    uint32_t i;
    FILE *fp;

    level_walk(lv, &t);
    /* pause menu graphic: 4bpp 128x96 (uploaded to VRAM 960,0 by FUN_80042df0). Its CLUT sits at VRAM (960,469),
     * which is inside the HUD sheet (loaded at 896,416): row 53, words 64..79 of the 8bpp sheet. */
    if (t.pause_len >= 0x1800 && t.pause_img + 0x1800 <= lv->ld_size) {
        uint8_t pal[32];
        wad_read(w, wad_hdr32(w, WAD_HUD_OFFSET) + 0x1400 + 53 * 256 + 128, pal, sizeof(pal));
        out_path(path, sizeof(path), subdir, "pause_menu.png");
        write_indexed4_png(path, lv->ld + t.pause_img, 128, 96, pal);
    }
    {
        /* player start (level header dwords 39..41, same units as entities) */
        unsigned sx = rd32(lv->lb + 39 * 4), sy = rd32(lv->lb + 40 * 4), sz = rd32(lv->lb + 41 * 4);
        out_path(path, sizeof(path), subdir, "level_info.txt");
        fp = xfopen(path, "w");
        fprintf(fp, "level: %d\nstart: %u %u %u\nstart_obj: %.1f %.1f %.1f\n", lv->num, sx, sy, sz, sx / 2.0, sz / 2.0, -(sy / 2.0));
        fclose(fp);
    }
    rip_level_map(lv, &t, subdir);
    rip_level_sky(w, lv, &t, subdir);
    /* mission objective text ('#' marks a paragraph break) */
    if (!lv->jp && t.text + t.text_len <= lv->ld_size && t.text_len) {
        out_path(path, sizeof(path), subdir, "objective.txt");
        fp = xfopen(path, "w");
        for (i = 0; i < t.text_len && lv->ld[t.text + i]; ++i) put_text_char(lv, (char)lv->ld[t.text + i], fp);
        fputc('\n', fp);
        fclose(fp);
    }
    wad_read(w, wad_hdr32(w, WAD_HUD_OFFSET) + 0x800 + 2 * 0x200, hudpal, sizeof(hudpal));
    if (lv->jp) {
        /* Japanese text: bytes index 1bpp glyphs of 13 columns x 13 rows (2 bytes per row, MSB first),
         * advance 14 px; 0xff = 6 px space, 0xfe = line break, 0 = end (FUN_80041fd4 in SLPS-00804). */
        uint32_t ng = t.font_big_len / 26, g, r, cc;
        if (ng && t.font_big + ng * 26 <= lv->ld_size) {
            uint8_t *strip = xcalloc((size_t)ng * 14 * 13, 1);
            for (g = 0; g < ng; ++g)
                for (r = 0; r < 13; ++r) {
                    uint16_t bits = (uint16_t)((lv->ld[t.font_big + g * 26 + r * 2] << 8) | lv->ld[t.font_big + g * 26 + r * 2 + 1]);
                    for (cc = 0; cc < 13; ++cc) if (bits & (0x8000 >> cc)) strip[(r * ng + g) * 14 + cc] = 1;
                }
            out_path(path, sizeof(path), subdir, "font_jp.png");
            write_indexed8_png(path, strip, ng * 14, 13, hudpal, 1);
            free(strip);
        }
        if (t.text + t.text_len <= lv->ld_size && t.text_len) {
            /* render the objective text to an image, since the glyph indices are level specific */
            int W = 320, H = 0, x = 0, y = 0, lines = 1;
            uint8_t *img;
            for (i = 0; i < t.text_len && lv->ld[t.text + i]; ++i) if (lv->ld[t.text + i] == 0xfe) ++lines;
            H = lines * 16;
            img = xcalloc((size_t)W * H, 1);
            for (i = 0; i < t.text_len && lv->ld[t.text + i]; ++i) {
                uint8_t ch = lv->ld[t.text + i];
                uint32_t r, cc;
                if (ch == 0xfe) { x = 0; y += 16; continue; }
                if (ch == 0xff) { x += 6; continue; }
                if ((uint32_t)ch * 26 + 26 > t.font_big_len || x + 14 > W) continue;
                for (r = 0; r < 13; ++r) {
                    uint16_t bits = (uint16_t)((lv->ld[t.font_big + ch * 26 + r * 2] << 8) | lv->ld[t.font_big + ch * 26 + r * 2 + 1]);
                    for (cc = 0; cc < 13; ++cc) if (bits & (0x8000 >> cc)) img[(size_t)(y + r) * W + x + cc] = 1;
                }
                x += 14;
            }
            out_path(path, sizeof(path), subdir, "objective.png");
            write_indexed8_png(path, img, W, H, hudpal, 1);
            free(img);
        }
    } else {
        /* fonts: 8x11 and 8x8 glyphs, drawn with the third HUD palette */
        write_glyph_strip(lv, t.font_big, t.font_big_len, 8, 11, hudpal, subdir, "font_big.png");
        write_glyph_strip(lv, t.font_small, t.font_small_len, 8, 8, hudpal, subdir, "font_small.png");
    }

    /* entity (enemy/object) placement: 0x68 bytes each. Byte 0x38 is the id of the room object the entity
     * lives in; what the entity *is* comes from its type definition record (pointer at +4, 0x60+ bytes,
     * byte 5 = class, byte 6 = subclass) together with the sprite set it draws from (+0x36). */
    if (t.entities + t.nent * 0x68 <= lv->ld_size) {
        uint32_t tdefs[256], ntdef = 0, k;
        FILE *ft;
        out_path(path, sizeof(path), subdir, "entities.txt");
        fp = xfopen(path, "w");
        fprintf(fp, "# index, x, y, z (game units), obj_x, obj_y, obj_z (level_%02d.obj space), room_id, sprite_set, area, difficulty_mask, typedef, class, subclass\n", lv->num);
        for (i = 0; i < t.nent; ++i) {
            const uint8_t *e = lv->ld + t.entities + i * 0x68;
            unsigned x = rd16(e + 0x16), y = rd16(e + 0x18), z = rd16(e + 0x28);
            uint32_t td = rd32(e + 4), idx;
            for (idx = 0; idx < ntdef && tdefs[idx] != td; ++idx) ;
            if (idx == ntdef && ntdef < 256) tdefs[ntdef++] = td;
            fprintf(fp, "%u, %u, %u, %u, %.1f, %.1f, %.1f, %u, %u, %u, %u, %u, %u, %u\n", i, x, y, z, x / 2.0, z / 2.0, -(y / 2.0),
                    e[0x38], e[0x36], e[0x39], e[0x4c], idx, td + 6 <= lv->ld_size ? lv->ld[td + 5] : 0, td + 7 <= lv->ld_size ? lv->ld[td + 6] : 0);
        }
        fclose(fp);
        /* the type definition records themselves, numbered as referenced above */
        out_path(path, sizeof(path), subdir, "typedefs.txt");
        ft = xfopen(path, "w");
        fprintf(ft, "# entity type definitions (0x60 bytes). Byte 5 is the movement/behaviour class used by the AI\n"
                    "# (FUN_800267f0): 0 static, 1 and 2 random turning within an angle range (bytes 0x29/0x14 and 0x2f/0x17),\n"
                    "# 3 patrol along a path (pointer at +0x28, length at +0x30), 4 free movement, 5 rotating sweep (speed at +0x2b).\n"
                    "# Byte 6 is the subclass (2 = enemy, 4/5 = objects with extra pointers at +0x48/+0x4c). What an entity looks\n"
                    "# like is given by its sprite set (entities.txt) and the set's object type records (Set NN - frames.txt).\n");
        for (k = 0; k < ntdef; ++k) {
            uint32_t b;
            if (tdefs[k] + 0x60 > lv->ld_size) continue;
            fprintf(ft, "%2u @0x%05X: ", k, tdefs[k]);
            for (b = 0; b < 0x60; ++b) fprintf(ft, "%02x%s", lv->ld[tdefs[k] + b], (b % 4 == 3) ? " " : "");
            fputc('\n', ft);
        }
        fclose(ft);
    }
    /* item (pickup) placement: 16 bytes each; the name comes from the item type's image header */
    if (t.items + t.nitems * 16 <= lv->ld_size) {
        out_path(path, sizeof(path), subdir, "items.txt");
        fp = xfopen(path, "w");
        fprintf(fp, "# index, x, y, z (game units), obj_x, obj_y, obj_z, item_type, difficulty_mask, name\n");
        for (i = 0; i < t.nitems; ++i) {
            const uint8_t *e = lv->ld + t.items + i * 16;
            unsigned x = rd16(e + 4), y = rd16(e + 6), z = rd16(e + 8), type = e[10];
            const char *nm = "?";
            if (type < 36 && t.itemtex + 36 * 4 <= lv->ld_size) {
                uint32_t q = rd32(lv->ld + t.itemtex + type * 4);
                if (q && q + 12 <= lv->ld_size) nm = level_string(lv, &t, lv->ld[q + 0xb]);
            }
            fprintf(fp, "%u, %u, %u, %u, %.1f, %.1f, %.1f, %u, %u, %s\n", i, x, y, z, x / 2.0, z / 2.0, -(y / 2.0), type, e[14], nm);
        }
        fclose(fp);
    }
    /* AI waypoint graph: 12 byte nodes (x, y, z?, flags, up to 4 links, 0xff = none) */
    if (t.waypoints + t.nwaypoints * 12 <= lv->ld_size && t.nwaypoints) {
        out_path(path, sizeof(path), subdir, "waypoints.txt");
        fp = xfopen(path, "w");
        fprintf(fp, "# index, x, y, z (game units), obj_x, obj_y, obj_z, byte6, byte7, link0, link1, link2, link3 (255 = none)\n");
        for (i = 0; i < t.nwaypoints; ++i) {
            const uint8_t *e = lv->ld + t.waypoints + i * 12;
            unsigned x = rd16(e), y = rd16(e + 2), z = rd16(e + 4);
            fprintf(fp, "%u, %u, %u, %u, %.1f, %.1f, %.1f, %u, %u, %u, %u, %u, %u\n", i, x, y, z, x / 2.0, z / 2.0, -(y / 2.0),
                    e[6], e[7], e[8], e[9], e[10], e[11]);
        }
        fclose(fp);
    }
    /* remaining tables whose meaning is only partly known, dumped as hex for further research */
    {
        out_path(path, sizeof(path), subdir, "tables.txt");
        fp = xfopen(path, "w");
        fprintf(fp, "# Raw dumps of the level data tables not decoded further yet. Offsets are into the level data\n"
                    "# (file offset = level block offset + 0x74800). See src/level.c for what is known about each.\n\n");
        dump_table(fp, lv, "scripted texture controllers: texture entry offset, cur, cur frame index, countdown", t.script_ctl, t.nscript_ctl, 8);
        dump_table(fp, lv, "scripted texture frames: 8 byte texture entry, next index A/B, flags, random, duration", t.script_frm, t.nscript_frm, 16);
        dump_table(fp, lv, "area scripts (area, difficulty, command, value)", t.t6, t.nt6, 16);
        dump_table(fp, lv, "t22: switchable objects (byte 9 = state)", t.t22, t.nt22, 12);
        if (t.doors + t.ndoors * 4 <= lv->ld_size) {
            fprintf(fp, "[door/lift controllers: byte 1 = state, byte 0xe = linked object type id] %u records\n", t.ndoors);
            for (i = 0; i < t.ndoors; ++i) {
                uint32_t q = rd32(lv->ld + t.doors + i * 4), k;
                if (q + 0x20 > lv->ld_size) continue;
                fprintf(fp, "%4u @0x%05X: ", i, q);
                for (k = 0; k < 0x20; ++k) fprintf(fp, "%02x%s", lv->ld[q + k], (k % 4 == 3) ? " " : "");
                fputc('\n', fp);
            }
            fputc('\n', fp);
        }
        dump_table(fp, lv, "t27: sprite/effect emitters (two level data pointers at +0 and +4)", t.t27, t.nt27, 36);
        dump_table(fp, lv, "t28", t.t28, t.nt28, 16);
        dump_table(fp, lv, "t31: item image headers (clut@2, x/16@4, y/16@5, w@6, h@7, name index@0xb)", t.t31, t.nt31, 12);
        fclose(fp);
    }
    /* scripted texture animation frames: each 16 byte record starts with a full texture entry */
    if (t.script_frm + t.nscript_frm * 16 <= lv->ld_size) {
        char texdir[160];
        uint8_t *rgba = xmalloc(TEX_SIZE * TEX_SIZE * 4);
        snprintf(texdir, sizeof(texdir), "%s/textures", subdir);
        for (i = 0; i < t.nscript_frm; ++i) {
            const uint8_t *e = lv->ld + t.script_frm + i * 16;
            if (!rd16(e + 6)) continue;
            psx_vram_decode(lv->vram, rd16(e + 6), rd16(e + 2), e[0], e[1], TEX_SIZE, TEX_SIZE, rgba);
            snprintf(name, sizeof(name), "script_frame_%03u.png", i);
            out_path(path, sizeof(path), texdir, name);
            png_write_rgba(path, TEX_SIZE, TEX_SIZE, rgba);
        }
        free(rgba);
    }
    /* item pickup images: 36 pointers (indexed by item type) to 12 byte headers: clut@2, x/16@4, y/16@5, w@6, h@7 */
    if (t.itemtex + 36 * 4 <= lv->ld_size) {
        for (i = 0; i < 36; ++i) {
            uint32_t q = rd32(lv->ld + t.itemtex + i * 4);
            const uint8_t *e;
            if (!q || q + 12 > lv->ld_size) continue;
            e = lv->ld + q;
            if (!e[6] || !e[7]) continue;
            snprintf(name, sizeof(name), "item_type_%02u.png", i);
            decode_vram_rect8(lv, e[4] * 16, e[5] * 16, rd16(e + 2), e[6], e[7], subdir, name);
        }
    }
    /* animated textures: 20 byte entries (frame list@0, texture index@4, frame count@0x11); frames are 12 bytes (clut@2, x/16@4, y/16@5) */
    if (t.texanim + t.ntexanim * 20 <= lv->ld_size) {
        char texdir[160];
        snprintf(texdir, sizeof(texdir), "%s/textures", subdir);
        for (i = 0; i < t.ntexanim; ++i) {
            const uint8_t *e = lv->ld + t.texanim + i * 20;
            uint32_t q = rd32(e), tex = rd32(e + 4), nf = e[0x11], f;
            if (q + nf * 12 > lv->ld_size) continue;
            for (f = 0; f < nf; ++f) {
                const uint8_t *fr = lv->ld + q + f * 12;
                snprintf(name, sizeof(name), "anim_%03u_frame%02u.png", tex, f + 1);
                decode_vram_rect8(lv, fr[4] * 16, fr[5] * 16, rd16(fr + 2), TEX_SIZE, TEX_SIZE, texdir, name);
            }
        }
    }
}

extern const unsigned char viewer_html[];
extern const size_t viewer_html_len;

/* Browser viewer for the exported levels, written next to the level folders. */
static void write_viewer(void) {
    char path[PATH_MAX_LEN];
    FILE *fp;
    out_path(path, sizeof(path), "Levels", "viewer.html");
    fp = xfopen(path, "wb");
    fwrite(viewer_html, 1, viewer_html_len, fp);
    fclose(fp);
    out_path(path, sizeof(path), "Levels", "README.txt");
    fp = xfopen(path, "w");
    fputs("viewer.html shows the extracted levels in a web browser (WASD + mouse, toggles for enemies, items,\n"
          "waypoints, sky and map). Browsers block file:// data loading, so serve this folder over HTTP, e.g.\n"
          "    cd \"Levels\" && python3 -m http.server 8000\n"
          "and open http://localhost:8000/viewer.html\n", fp);
    fclose(fp);
}

void rip_levels(wad_t *w, int want_textures, int want_geometry) {
    int num;
    write_viewer();
    for (num = 1; num <= WAD_MAX_LEVEL; ++num) {
        level_t lv;
        char subdir[64];
        if (!level_load(w, num, &lv)) continue;
        snprintf(subdir, sizeof(subdir), "Levels/Level %02d", num);
        if (want_textures) {
            printf("Ripping level %d textures (%u)...\n", num, lv.ntex);
            rip_level_textures(&lv, subdir);
            rip_level_extras(w, &lv, subdir);
        }
        if (want_geometry) {
            printf("Ripping level %d geometry...\n", num);
            rip_level_geometry(w, &lv, subdir);
        }
        level_free(&lv);
    }
}
