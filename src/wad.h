#ifndef WAD_H
#define WAD_H

#include "common.h"

/* Layout of WAD.IN (PAL SLES-00535). All offsets are absolute file positions.
 * The first 0x654 bytes are a table of offsets which the game copies into RAM at 0x80075b70. */
#define WAD_HEADER_SIZE        0x654
#define WAD_HUD_OFFSET         0x000   /* u32 offset, u32 size of the HUD block                     */
#define WAD_WEAPON_TABLE       0x008   /* 8 groups x 10 weapon blocks, group = (level+1)/2 (all identical copies) */
#define WAD_PSYONIC_TABLE      0x148   /* 8 groups x 10 psyonic blocks (6 used)                      */
#define WAD_LEVEL_TABLE        0x288   /* level N (1..15) offset at 0x288 + N*4                      */
#define WAD_MUSIC_TABLE        0x2C8   /* level N entry at 0x2C8 + N*0x20                            */
#define WAD_TITLE_TABLE        0x4C8   /* 5 title/menu blocks of 0x92000 bytes                       */
#define WAD_TITLE_SND_TABLE    0x4DC   /* sound blocks for the 5 title variants                      */
#define WAD_DUMMY_WEAPON_BLOCK 0x4F0   /* 0x11000 byte placeholder weapon block (filler pattern)     */
#define WAD_SPEECH_TABLE       0x4F4   /* level N: u32 offset, u32 size at 0x4F4 + N*8               */
#define WAD_FMV_AUDIO_TABLE    0x574   /* movie N audio at 0x574 + N*4                               */
#define WAD_DEMO_TABLE         0x5C4   /* level N attract-mode input recording at 0x5C4 + N*4        */
#define WAD_BANNER_TABLE       0x604   /* level N 320x32 loading screen hint at 0x604 + N*4          */
#define WAD_SPLASH_TABLE       0x644   /* 4 boot splash screens, 480x256 16bpp                       */

#define WAD_MAX_LEVEL          15
/* Level block: 0x800 header + a small VRAM image + the 704x320 VRAM image. PAL: 64x192 sprite page at (896,0),
 * total 0x74800. NTSC: 320x32 loading hint strip at (0,480), total 0x73800. See wad_level_block_size(). */

typedef enum { WAD_REGION_UNKNOWN = 0, WAD_REGION_PAL, WAD_REGION_NTSC, WAD_REGION_PAL_DEMO, WAD_REGION_NTSC_J, WAD_REGION_NTSC_DEMO, WAD_REGION_PAL_DE, WAD_REGION_PAL_FR } wad_region_t;

typedef struct wad_s {
    FILE *fp;
    uint64_t size;
    uint8_t header[WAD_HEADER_SIZE];
    wad_region_t region;     /* PAL (SLES-00535), NTSC-U (SLUS-00224) or the PAL demo disc (SLED-00594); detected from the header */
    char path[PATH_MAX_LEN]; /* path of the archive, used to find WARNING.IN next to it */
} wad_t;

int  wad_open(wad_t *w, const char *path);
const char *wad_region_name(const wad_t *w);
/* NTSC-U and NTSC-J share the 240 line layouts and the 0x73800 level block */
static inline int wad_is_ntsc(const wad_t *w) { return w->region == WAD_REGION_NTSC || w->region == WAD_REGION_NTSC_J || w->region == WAD_REGION_NTSC_DEMO; }
static inline int wad_is_jp(const wad_t *w) { return w->region == WAD_REGION_NTSC_J; }
static inline int wad_is_usdemo(const wad_t *w) { return w->region == WAD_REGION_NTSC_DEMO; }
static inline int wad_is_german(const wad_t *w) { return w->region == WAD_REGION_PAL_DE; }
static inline int wad_is_french(const wad_t *w) { return w->region == WAD_REGION_PAL_FR; }
/* The US demo (September 1996 build) has a 0x5FC byte header: four title entries, speech at 0x4EC, no FMV/hint/splash tables. */
static inline size_t wad_speech_table(const wad_t *w) { return wad_is_usdemo(w) ? 0x4EC : WAD_SPEECH_TABLE; }
static inline size_t wad_title_snd_table(const wad_t *w) { return wad_is_usdemo(w) ? 0x4D8 : WAD_TITLE_SND_TABLE; }
static inline int wad_title_count(const wad_t *w) { return wad_is_usdemo(w) ? 4 : 5; }
/* Index (from dword 70 of the level header) of the SPU sound data size: the Japanese header lost one table,
 * the US demo header has a shorter tail. */
static inline int wad_level_sndsize_index(const wad_t *w) { return wad_is_jp(w) ? 39 : wad_is_usdemo(w) ? 37 : 40; }
static inline uint32_t wad_level_block_size(const wad_t *w) { return wad_is_ntsc(w) ? 0x73800 : 0x74800; }
void wad_close(wad_t *w);

/* Read n bytes at offset into buf. Short reads (past EOF) are zero filled and return 0. */
int  wad_read(wad_t *w, uint64_t off, void *buf, size_t n);
/* Read n bytes at offset into a freshly allocated buffer. */
uint8_t *wad_read_alloc(wad_t *w, uint64_t off, size_t n);
uint32_t wad_u32(wad_t *w, uint64_t off);

/* Header accessors */
static inline uint32_t wad_hdr32(const wad_t *w, size_t off) { return rd32(w->header + off); }
static inline uint32_t wad_level_offset(const wad_t *w, int level) { return wad_hdr32(w, WAD_LEVEL_TABLE + level * 4); }

/* The weapon and psyonic tables hold 8 groups (one per pair of levels); retail archives fill all groups with
 * identical copies, the demo only the groups of the levels it ships. Returns the first non-empty group. */
int wad_first_group(const wad_t *w, size_t table, int per_group);

/* Shared output state */
extern char g_outdir[PATH_MAX_LEN];
void out_path(char *dst, size_t n, const char *subdir, const char *name);  /* creates subdir */

#endif
