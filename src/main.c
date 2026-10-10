/* disruptor_ripper: asset extractor for Disruptor (PlayStation, PAL SLES-00535) WAD.IN archives. */
#include <stdarg.h>
#include "wad.h"
#include "platform.h"
#include "rippers.h"

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("Out of memory (%zu bytes)", n); return p; }
void *xcalloc(size_t n, size_t sz) { void *p = calloc(n ? n : 1, sz ? sz : 1); if (!p) die("Out of memory"); return p; }
FILE *xfopen(const char *path, const char *mode) { FILE *f = fopen(path, mode); if (!f) die("Couldn't open %s", path); return f; }

enum {
    OPT_ENEMIES = 1 << 0, OPT_WEAPONS = 1 << 1, OPT_HUD = 1 << 2, OPT_SOUNDS = 1 << 3,
    OPT_MUSIC = 1 << 4, OPT_FMV = 1 << 5, OPT_TEXTURES = 1 << 6, OPT_GEOMETRY = 1 << 7,
    OPT_MISC = 1 << 8, OPT_TEXT = 1 << 9,
    OPT_ALL = 0x3ff
};

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [options] <path to WAD.IN>\n"
        "\n"
        "Extracts assets from Disruptor's WAD.IN (PAL, NTSC-U, NTSC-J or PAL demo disc, detected automatically).\n"
        "With no selection options everything is extracted.\n"
        "\n"
        "  -o, --out DIR    output directory (default: \"Disruptor resources\" next to WAD.IN)\n"
        "      --enemies    enemy sprites\n"
        "      --weapons    weapon and psyonic sprites\n"
        "      --hud        HUD sheet\n"
        "      --sounds     sound effects and speech (.vag + .wav)\n"
        "      --music      streamed music (.vag + .wav)\n"
        "      --fmv        FMV voice-over audio\n"
        "      --textures   level textures, texture atlases and VRAM snapshots\n"
        "      --geometry   level geometry (.obj/.mtl, textured)\n"
        "      --misc       splash screens, title screens, loading screen hints\n"
        "      --text       menu strings, mission names, objectives and item names (Text/)\n"
        "      --exe FILE   also dump the printable strings of the game executable into Text/\n"
        "  -h, --help       this text\n", argv0);
}

int main(int argc, char **argv) {
    const char *wad_path = NULL, *out = NULL, *exe_path = NULL;
    unsigned opts = 0;
    wad_t wad;
    int i;

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--enemies")) opts |= OPT_ENEMIES;
        else if (!strcmp(a, "--weapons")) opts |= OPT_WEAPONS;
        else if (!strcmp(a, "--hud")) opts |= OPT_HUD;
        else if (!strcmp(a, "--sounds")) opts |= OPT_SOUNDS;
        else if (!strcmp(a, "--music")) opts |= OPT_MUSIC;
        else if (!strcmp(a, "--fmv")) opts |= OPT_FMV;
        else if (!strcmp(a, "--textures")) opts |= OPT_TEXTURES;
        else if (!strcmp(a, "--geometry")) opts |= OPT_GEOMETRY;
        else if (!strcmp(a, "--misc")) opts |= OPT_MISC;
        else if (!strcmp(a, "--text")) opts |= OPT_TEXT;
        else if (!strcmp(a, "--exe") && i + 1 < argc) exe_path = argv[++i];
        else if (!strcmp(a, "--all")) opts |= OPT_ALL;
        else if (a[0] == '-' && a[1]) { fprintf(stderr, "Unknown option %s\n", a); usage(argv[0]); return 1; }
        else if (!wad_path) wad_path = a;
        else { usage(argv[0]); return 1; }
    }
    if (!wad_path) { usage(argv[0]); return 1; }
    if (!opts) opts = OPT_ALL;

    if (!wad_open(&wad, wad_path)) die("Couldn't open %s", wad_path);
    if (wad.region == WAD_REGION_UNKNOWN)
        fprintf(stderr, "Warning: %s is not a known Disruptor WAD.IN (PAL, NTSC-U, NTSC-J or PAL demo); results may be wrong.\n", wad_path);
    printf("Archive region: %s\n", wad_region_name(&wad));

    if (out) {
        snprintf(g_outdir, sizeof(g_outdir), "%s", out);
    } else {
        char dir[PATH_MAX_LEN];
        path_dirname(dir, sizeof(dir), wad_path);
        path_join(g_outdir, sizeof(g_outdir), dir, "Disruptor resources");
    }
    make_dirs(g_outdir);
    printf("Extracting to %s\n", g_outdir);

    if (opts & OPT_MISC)     { rip_splash(&wad); rip_warning_screen(&wad); rip_banners(&wad); rip_title(&wad); }
    if (opts & OPT_HUD)      rip_hud(&wad);
    if (opts & OPT_WEAPONS)  { rip_psyonics(&wad); rip_weapons(&wad); }
    if (opts & OPT_ENEMIES)  rip_enemy_sprites(&wad);
    if (opts & (OPT_TEXTURES | OPT_GEOMETRY)) rip_levels(&wad, opts & OPT_TEXTURES, opts & OPT_GEOMETRY);
    if (opts & OPT_SOUNDS)   rip_sounds(&wad);
    if (opts & OPT_FMV)      rip_fmv_audio(&wad);
    if (opts & OPT_MUSIC)    rip_music(&wad);
    if (opts & OPT_TEXT)     { rip_title_text(&wad); rip_text_levels(&wad); }
    if (exe_path)            rip_exe_strings(exe_path);

    wad_close(&wad);
    puts("Done.");
    return 0;
}
