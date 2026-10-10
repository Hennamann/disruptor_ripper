#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "wad.h"
#include "platform.h"

char g_outdir[PATH_MAX_LEN];

int wad_open(wad_t *w, const char *path) {
    memset(w, 0, sizeof(*w));
    w->fp = fopen(path, "rb");
    if (!w->fp) return 0;
    fseek(w->fp, 0, SEEK_END);
    w->size = (uint64_t)ftell(w->fp);
    fseek(w->fp, 0, SEEK_SET);
    if (fread(w->header, 1, WAD_HEADER_SIZE, w->fp) != WAD_HEADER_SIZE) {
        fclose(w->fp);
        w->fp = NULL;
        return 0;
    }
    /* the HUD block (first table entry) sits at a region specific offset */
    if (wad_hdr32(w, WAD_HUD_OFFSET) == 0x11A000 && wad_level_offset(w, 1) == 0x121800) {
        /* The German (SLES-00565) and French (SLES-00564) releases share the PAL layout. Both have larger
         * title blocks; the French master additionally packs level 2 one sector earlier. */
        if (wad_level_offset(w, 2) == 0x9ED000) w->region = WAD_REGION_PAL_FR;
        else if (wad_hdr32(w, WAD_TITLE_TABLE + 4) - wad_hdr32(w, WAD_TITLE_TABLE) == 0xEA800) w->region = WAD_REGION_PAL_DE;
        else w->region = WAD_REGION_PAL;
    }
    else if (wad_hdr32(w, WAD_HUD_OFFSET) == 0xD3800 && wad_level_offset(w, 1) == 0xDB000) {
        /* NTSC-U and NTSC-J start identically; the Japanese hint strips are 320x64 (0xA000 apart) */
        w->region = (wad_hdr32(w, WAD_BANNER_TABLE + 8) - wad_hdr32(w, WAD_BANNER_TABLE + 4) == 0xA000) ? WAD_REGION_NTSC_J : WAD_REGION_NTSC;
    }
    else if (wad_hdr32(w, WAD_HUD_OFFSET) == 0x800 && wad_level_offset(w, 3) == 0x8000)
        w->region = wad_level_offset(w, 4) ? WAD_REGION_PAL_DEMO : WAD_REGION_NTSC_DEMO;   /* the US demo has no level 4 but has level 15 */
    else w->region = WAD_REGION_UNKNOWN;
    snprintf(w->path, sizeof(w->path), "%s", path);
    return 1;
}

const char *wad_region_name(const wad_t *w) {
    switch (w->region) {
    case WAD_REGION_PAL: return "PAL (SLES-00535)";
    case WAD_REGION_NTSC: return "NTSC-U (SLUS-00224)";
    case WAD_REGION_PAL_DEMO: return "PAL demo disc (SLED-00594)";
    case WAD_REGION_NTSC_J: return "NTSC-J (SLPS-00804)";
    case WAD_REGION_NTSC_DEMO: return "US demo disc (DEMO.EXE, September 1996)";
    case WAD_REGION_PAL_DE: return "PAL German (SLES-00565)";
    case WAD_REGION_PAL_FR: return "PAL French (SLES-00564)";
    default: return "unknown";
    }
}

int wad_first_group(const wad_t *w, size_t table, int per_group) {
    int g;
    for (g = 0; g < 8; ++g)
        if (wad_hdr32(w, table + (size_t)g * per_group * 4)) return g;
    return 0;
}

void wad_close(wad_t *w) {
    if (w->fp) fclose(w->fp);
    w->fp = NULL;
}

int wad_read(wad_t *w, uint64_t off, void *buf, size_t n) {
    size_t got = 0;
    if (off < w->size) {
#ifdef _WIN32
        _fseeki64(w->fp, (long long)off, SEEK_SET);
#else
        fseeko(w->fp, (off_t)off, SEEK_SET);
#endif
        got = fread(buf, 1, n, w->fp);
    }
    if (got < n) memset((uint8_t *)buf + got, 0, n - got);
    return got == n;
}

uint8_t *wad_read_alloc(wad_t *w, uint64_t off, size_t n) {
    uint8_t *b = xmalloc(n ? n : 1);
    wad_read(w, off, b, n);
    return b;
}

uint32_t wad_u32(wad_t *w, uint64_t off) {
    uint8_t b[4];
    wad_read(w, off, b, 4);
    return rd32(b);
}

void out_path(char *dst, size_t n, const char *subdir, const char *name) {
    char dir[PATH_MAX_LEN];
    path_join(dir, sizeof(dir), g_outdir, subdir);
    make_dirs(dir);
    path_join(dst, n, dir, name);
}
