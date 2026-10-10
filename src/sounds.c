/* Sound effects, speech, streamed music and FMV audio.
 * All audio in WAD.IN is raw Sony SPU-ADPCM (16 byte blocks); we save it both as .vag and decoded .wav. */
#include "rippers.h"
#include "adpcm.h"

#define SND_DESC_SIZE 16

static uint32_t pitch_to_rate(uint32_t pitch) { return (uint32_t)((uint64_t)pitch * 44100 / 4096); }

static void save_sound(const char *subdir, const char *name, const uint8_t *data, size_t n, uint32_t rate, int stop_at_end) {
    char path[PATH_MAX_LEN], fname[160];
    snprintf(fname, sizeof(fname), "%s.vag", name);
    out_path(path, sizeof(path), subdir, fname);
    vag_write(path, data, n, rate, name);
    snprintf(fname, sizeof(fname), "%s.wav", name);
    out_path(path, sizeof(path), subdir, fname);
    adpcm_write_wav(path, data, n, rate, stop_at_end);
}

/* Split a bank of concatenated samples on the "end" block (flags byte 7) the game uses as terminator. */
static void split_bank(const char *subdir, const char *prefix, const uint8_t *data, size_t n, uint32_t (*rate_fn)(int, int), int bank) {
    size_t start = 0, pos = 0;
    int count = 1;
    char name[128];
    while (pos + 16 <= n) {
        if (data[pos + 1] == 7 && rd16(data + pos + 2) == 0x7777) {
            size_t end = pos + 16;
            snprintf(name, sizeof(name), "%s %02d", prefix, count);
            save_sound(subdir, name, data + start, end - start, rate_fn(bank, count), 1);
            ++count;
            start = end;
            if (end + 32 > n || data[end + 16] == 0) break;   /* bank ends with zero padding */
            pos = end;
        } else {
            pos += 16;
        }
    }
}

static uint32_t fixed_22050(int a, int b) { (void)a; (void)b; return 22050; }

void rip_sounds(wad_t *w) {
    int level, i;
    char subdir[128], name[128];

    /* ---- in-game sounds: one bank per level, table of 16 byte descriptors right before the data */
    for (level = 1; level <= WAD_MAX_LEVEL; ++level) {
        uint32_t loff = wad_level_offset(w, level), data_off, data_size, count = 0, j;
        uint8_t hdr[0x800], *table, *data;
        if (!loff) continue;
        wad_read(w, loff, hdr, sizeof(hdr));
        data_off = loff + wad_level_block_size(w) + rd32(hdr);   /* LB[0]: start of SPU data inside the level data */
        data_size = rd32(hdr + (70 + wad_level_sndsize_index(w)) * 4);   /* size of SPU data */
        /* count descriptors backwards until the entry whose offset is 0 (the first sample) */
        for (;;) {
            uint8_t d[SND_DESC_SIZE];
            ++count;
            wad_read(w, data_off - count * SND_DESC_SIZE, d, SND_DESC_SIZE);
            if (rd32(d) == 0 || count > 512) break;
        }
        table = wad_read_alloc(w, data_off - count * SND_DESC_SIZE, count * SND_DESC_SIZE);
        data = wad_read_alloc(w, data_off, data_size);
        snprintf(subdir, sizeof(subdir), "Sounds/Level %02d", level);
        printf("Ripping level %d sounds (%u)...\n", level, count);
        for (j = 0; j < count; ++j) {
            uint32_t off = rd32(table + j * SND_DESC_SIZE), end = (j + 1 < count) ? rd32(table + (j + 1) * SND_DESC_SIZE) : data_size;
            uint32_t pitch = rd32(table + j * SND_DESC_SIZE + 8);
            if (end > data_size) end = data_size;
            if (off >= end) continue;
            snprintf(name, sizeof(name), "Level%02d snd%02u", level, j + 1);
            save_sound(subdir, name, data + off, end - off, pitch_to_rate(pitch ? pitch : 0x800), 1);
        }
        free(table);
        free(data);
    }

    /* ---- loading screen speeches (one per level, 22050 Hz) */
    printf("Ripping speeches...\n");
    for (level = 0; level <= WAD_MAX_LEVEL; ++level) {
        uint32_t off = wad_hdr32(w, wad_speech_table(w) + level * 8), size = wad_hdr32(w, wad_speech_table(w) + level * 8 + 4);
        uint8_t *data;
        if (!off || !size || size > 0x400000) continue;
        data = wad_read_alloc(w, off, size);
        snprintf(name, sizeof(name), "Speech level %02d", level);
        save_sound("Sounds/Loading screen speeches", name, data, size, 22050, 1);
        free(data);
    }

    /* ---- weapon sounds: the block header (dword 4, relative to +0x11000) points at a slot table of
     * 16 byte descriptors (offset into the bank at +0x9708, volume, SPU pitch, 0); dword 9 is the slot
     * count. Unused slots are zero and several slots may share one sample, so collect distinct offsets. */
    printf("Ripping weapon sounds...\n");
    for (i = 0; i < 10; ++i) {
        uint32_t boff = wad_hdr32(w, WAD_WEAPON_TABLE + (wad_first_group(w, WAD_WEAPON_TABLE, 10) * 10 + i) * 4);
        uint8_t *blk = wad_read_alloc(w, boff, 0x12000);
        uint8_t *bank = wad_read_alloc(w, boff + 0x9708, 0x7D800);
        uint32_t tbl = 0x11000 + rd32(blk + 0x10), nslots = rd32(blk + 0x24), k, nsnd = 0;
        uint32_t offs[64], pitches[64];
        if (nslots > 64) nslots = 64;
        for (k = 0; k < nslots && tbl + (k + 1) * 16 <= 0x12000; ++k) {
            uint32_t o = rd32(blk + tbl + k * 16), pitch = rd32(blk + tbl + k * 16 + 8), m;
            int dup = 0;
            if (!pitch || pitch > 0x1000 || o >= 0x7D800 || (o & 15)) continue;
            for (m = 0; m < nsnd; ++m) if (offs[m] == o) dup = 1;
            if (dup) continue;
            /* keep sorted by offset */
            for (m = nsnd; m > 0 && offs[m - 1] > o; --m) { offs[m] = offs[m - 1]; pitches[m] = pitches[m - 1]; }
            offs[m] = o; pitches[m] = pitch; ++nsnd;
        }
        for (k = 0; k < nsnd; ++k) {
            uint32_t end = (k + 1 < nsnd) ? offs[k + 1] : 0;
            if (!end) {   /* last sample: run to its end-marker block */
                for (end = offs[k]; end + 16 <= 0x7D800; end += 16)
                    if (bank[end + 1] == 7 && rd16(bank + end + 2) == 0x7777) { end += 16; break; }
            }
            if (end <= offs[k]) continue;
            snprintf(name, sizeof(name), "Weapon %02d snd %02u", i + 1, k + 1);
            save_sound("Sounds/Weapon sounds", name, bank + offs[k], end - offs[k], pitch_to_rate(pitches[k]), 1);
        }
        free(bank);
        free(blk);
    }

    /* ---- psyonic sounds: 16 byte descriptors (offset, volume, pitch, 0) sit right before the SPU data
     * (block + dword 5 + 0x1000), the first one having offset 0; fall back to the end markers if absent */
    printf("Ripping psyonic sounds...\n");
    for (i = 0; i < 6; ++i) {
        uint32_t boff = wad_hdr32(w, WAD_PSYONIC_TABLE + (wad_first_group(w, WAD_PSYONIC_TABLE, 10) * 10 + i) * 4), rel = wad_u32(w, boff + 0x14);
        uint32_t data = boff + rel + 0x1000, count = 0, j;
        uint8_t *bank = wad_read_alloc(w, data, 0x8000), *table;
        for (;;) {
            uint8_t d[SND_DESC_SIZE];
            ++count;
            wad_read(w, data - count * SND_DESC_SIZE, d, SND_DESC_SIZE);
            if (rd32(d) == 0 || count > 64) break;
        }
        table = wad_read_alloc(w, data - count * SND_DESC_SIZE, count * SND_DESC_SIZE);
        if (count > 64 || rd32(table + 8) == 0) {
            snprintf(name, sizeof(name), "Psyonic %d snd", i + 1);
            split_bank("Sounds/Psyonic sounds", name, bank, 0x8000, fixed_22050, i);
        } else {
            for (j = 0; j < count; ++j) {
                uint32_t off = rd32(table + j * SND_DESC_SIZE), pitch = rd32(table + j * SND_DESC_SIZE + 8);
                uint32_t end = (j + 1 < count) ? rd32(table + (j + 1) * SND_DESC_SIZE) : 0;
                if (!end) for (end = off; end + 16 <= 0x8000; end += 16) if (bank[end + 1] == 7 && rd16(bank + end + 2) == 0x7777) { end += 16; break; }
                if (end > 0x8000 || off >= end) continue;
                snprintf(name, sizeof(name), "Psyonic %d snd %02u", i + 1, j + 1);
                save_sound("Sounds/Psyonic sounds", name, bank + off, end - off, pitch_to_rate(pitch ? pitch : 0x800), 1);
            }
        }
        free(table);
        free(bank);
    }
}

/* ---------------------------------------------------------------- music
 * Music is streamed from the CD in 0xC000 byte chunks into an SPU ring buffer (FUN_80018d80).
 * Each level owns up to three songs; every song has up to three "A" variants and three "B"
 * variants (the game switches between the groups depending on combat). Track sizes and loop
 * points live in tables inside the executable, indexed by [level*3 + song]. */
static const int16_t music_size_a[48] = {
    0x165,-1,-1, 0x165,-1,-1, 0x14e,-1,-1, 0x1b1,-1,-1, 0xd7,0xd7,-1, 0x189,0x16c,0x177, 0x172,0x1e6,-1,
    0x238,-1,-1, 0x186,0xf0,-1, 0x157,-1,-1, 0x165,-1,-1, 0x191,-1,-1, 0x1b8,0x1a1,-1, 0x165,-1,-1,
    0x19e,-1,-1, 0x116,0x175,0x380 };
static const int16_t music_size_b[48] = {
    0x1b2,-1,-1, 0x1b2,-1,-1, 0x1c5,-1,-1, 0x191,-1,-1, 0x157,0x129,-1, 0x18a,0x13a,0x17a, 0x205,0x245,-1,
    0x238,-1,-1, 0x15a,0x130,-1, 0x1e5,-1,-1, 0x1b2,-1,-1, 0x17a,-1,-1, 0x205,0x177,-1, 0x1b2,-1,-1,
    0x18c,-1,-1, 0x17e,0x175,0x380 };
static const int16_t music_loop_a[48] = {
    18,-1,-1, 18,-1,-1, 19,-1,-1, 11,-1,-1, 18,18,-1, 19,5,38, 22,43,-1, 0,-1,-1, 15,12,-1, 12,-1,-1,
    18,-1,-1, 21,-1,-1, 46,23,-1, 18,-1,-1, 20,-1,-1, 8,11,21 };
static const int16_t music_loop_b[48] = {
    21,-1,-1, 21,-1,-1, 10,-1,-1, 21,-1,-1, 5,37,-1, 6,11,19, 11,7,-1, 0,-1,-1, 7,3,-1, 6,-1,-1,
    21,-1,-1, 6,-1,-1, 11,11,-1, 21,-1,-1, 8,-1,-1, 6,11,21 };

#define MUSIC_CHUNK   0xC000
#define MUSIC_RATE    22050

static void save_track(const char *subdir, const char *name, wad_t *w, uint64_t off, size_t len, int loop_sectors, FILE *info) {
    uint8_t *data = wad_read_alloc(w, off, len);
    size_t loop_bytes = (size_t)loop_sectors * 0x800 + MUSIC_CHUNK;
    save_sound(subdir, name, data, len, MUSIC_RATE, 0);
    fprintf(info, "%s: file offset 0x%llX, %zu bytes, loop start at %zu samples (%.2f s), length %.2f s\n",
            name, (unsigned long long)off, len, loop_bytes / 16 * 28, (double)(loop_bytes / 16 * 28) / MUSIC_RATE,
            (double)(len / 16 * 28) / MUSIC_RATE);
    free(data);
}

void rip_music(wad_t *w) {
    int level;
    char subdir[128], name[128], path[PATH_MAX_LEN];
    for (level = 1; level <= WAD_MAX_LEVEL; ++level) {
        const uint8_t *e = w->header + WAD_MUSIC_TABLE + level * 0x20;
        uint64_t base = rd32(e), p;
        int song;
        FILE *info;
        if (!base) continue;
        snprintf(subdir, sizeof(subdir), "Music/Level %02d", level);
        out_path(path, sizeof(path), subdir, "tracks.txt");
        info = xfopen(path, "w");
        printf("Ripping level %d music...\n", level);
        /* the first 0x18000 bytes are the two chunks pre-loaded when the level starts (intro of song 1) */
        save_track(subdir, "Song 1 - Intro", w, base, 0x18000, -24, info);
        p = base + 0x18000;
        for (song = 0; song < 3; ++song) {
            int na = (int)rd32(e + 4 + song * 8), nb = (int)rd32(e + 8 + song * 8), i;
            if (na < 0 || na > 3) na = 0;   /* the demo marks unused songs with 0xFFFFFFFF */
            if (nb < 0 || nb > 3) nb = 0;
            int sa = music_size_a[level * 3 + song], sb = music_size_b[level * 3 + song];
            for (i = 0; i < na && sa >= 0; ++i) {
                snprintf(name, sizeof(name), "Song %d - Track A%d", song + 1, i + 1);
                save_track(subdir, name, w, p, (size_t)sa * 0x800 + MUSIC_CHUNK, music_loop_a[level * 3 + song], info);
                p += (uint64_t)sa * 0x800 + 2 * MUSIC_CHUNK;
            }
            for (i = 0; i < nb && sb >= 0; ++i) {
                snprintf(name, sizeof(name), "Song %d - Track B%d", song + 1, i + 1);
                save_track(subdir, name, w, p, (size_t)sb * 0x800 + MUSIC_CHUNK, music_loop_b[level * 3 + song], info);
                p += (uint64_t)sb * 0x800 + 2 * MUSIC_CHUNK;
            }
        }
        fclose(info);
    }
}

/* ---------------------------------------------------------------- FMV audio
 * The movies (MOVIE%d.STR) are silent; their audio is streamed from WAD.IN (FUN_80014500).
 * Sizes (in CD sectors) and SPU pitch come from tables in the executable. */
static const int16_t fmv_sectors_pal[17]  = {330, 0, 0, 3069, 1470, 1896, 2002, 3586, 2364, 2975, 1248, 1880, 2851, 3678, 634, 642, 795};
static const uint16_t fmv_pitch_pal[17]   = {2048, 2048, 2048, 2042, 2042, 2042, 2039, 1685, 2042, 2041, 2042, 2039, 2041, 1683, 2042, 2042, 2042};
static const int16_t fmv_sectors_ntsc[17] = {360, 0, 0, 3679, 1759, 2272, 2396, 4302, 2832, 3567, 1499, 2258, 3414, 4407, 760, 764, 955};
static const uint16_t fmv_pitch_ntsc[17]  = {2048, 2048, 2048, 2043, 2048, 2048, 2048, 1688, 2048, 2048, 2048, 2048, 2048, 1688, 2048, 2048, 2048};

/* Smallest header offset strictly greater than off (used to bound regions whose size is not stored). */
static uint32_t wad_next_offset(wad_t *w, uint32_t off) {
    uint32_t best = (uint32_t)w->size, i;
    for (i = 0; i + 4 <= WAD_HEADER_SIZE; i += 4) {
        uint32_t v = rd32(w->header + i);
        if (v > off && v < best && v != 0xFFFFFFFF && (v & 0x7ff) == 0) best = v;
    }
    return best;
}

void rip_fmv_audio(wad_t *w) {
    int i;
    char name[128];
    if (wad_is_usdemo(w)) return;   /* no FMV table in the demo header */
    const int16_t *fmv_sectors = wad_is_ntsc(w) ? fmv_sectors_ntsc : fmv_sectors_pal;
    const uint16_t *fmv_pitch = wad_is_ntsc(w) ? fmv_pitch_ntsc : fmv_pitch_pal;
    printf("Ripping FMV audio...\n");
    for (i = 0; i < 17; ++i) {
        uint32_t off = wad_hdr32(w, WAD_FMV_AUDIO_TABLE + i * 4), limit;
        size_t len = (size_t)fmv_sectors[i] * 0x800;
        uint8_t *data;
        if (!off || off == 0xFFFFFFFF || !len) continue;
        limit = wad_next_offset(w, off);
        if (off + len > limit) len = limit - off;
        data = wad_read_alloc(w, off, len);
        snprintf(name, sizeof(name), "Movie %02d audio", i);
        save_sound("Sounds/FMV audio", name, data, len, pitch_to_rate(fmv_pitch[i]), 0);
        free(data);
    }
}
