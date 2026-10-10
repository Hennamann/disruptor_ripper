#include "adpcm.h"

int vag_write(const char *path, const uint8_t *data, size_t n, uint32_t sample_rate, const char *name) {
    uint8_t hdr[48];
    FILE *fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "Couldn't create %s\n", path); return 0; }
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "VAGp", 4);
    wr32be(hdr + 4, 0x20);
    wr32be(hdr + 12, (uint32_t)n);
    wr32be(hdr + 16, sample_rate);
    snprintf((char *)hdr + 32, 16, "%s", name ? name : "");
    fwrite(hdr, 1, sizeof(hdr), fp);
    fwrite(data, 1, n, fp);
    fclose(fp);
    return 1;
}

static const int filt_pos[5] = {0, 60, 115, 98, 122};
static const int filt_neg[5] = {0, 0, -52, -55, -60};

int adpcm_write_wav(const char *path, const uint8_t *data, size_t n, uint32_t sample_rate, int stop_at_end_flag) {
    size_t nblocks = n / 16, i, j, nsamples = nblocks * 28, written = 0;
    int16_t *pcm = xmalloc(nsamples * 2 + 2);
    int s1 = 0, s2 = 0;
    uint8_t hdr[44];
    FILE *fp;

    for (i = 0; i < nblocks; ++i) {
        const uint8_t *b = data + i * 16;
        int shift = b[0] & 15, filter = (b[0] >> 4) & 7, flags = b[1];
        if (filter > 4) filter = 4;
        if (shift > 12) shift = 9;
        if (flags == 7) break;           /* stream end marker used by some tools */
        for (j = 0; j < 28; ++j) {
            int nib = (j & 1) ? (b[2 + j / 2] >> 4) : (b[2 + j / 2] & 15);
            int s = (int16_t)(nib << 12) >> shift;
            s += (s1 * filt_pos[filter] + s2 * filt_neg[filter] + 32) >> 6;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            s2 = s1; s1 = s;
            pcm[written++] = (int16_t)s;
        }
        if (stop_at_end_flag && (flags & 1)) break;
    }

    fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "Couldn't create %s\n", path); free(pcm); return 0; }
    memcpy(hdr, "RIFF", 4); wr32(hdr + 4, (uint32_t)(36 + written * 2));
    memcpy(hdr + 8, "WAVEfmt ", 8); wr32(hdr + 16, 16);
    wr16(hdr + 20, 1); wr16(hdr + 22, 1);
    wr32(hdr + 24, sample_rate); wr32(hdr + 28, sample_rate * 2);
    wr16(hdr + 32, 2); wr16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4); wr32(hdr + 40, (uint32_t)(written * 2));
    fwrite(hdr, 1, 44, fp);
    for (i = 0; i < written; ++i) { uint8_t s[2]; wr16(s, (uint16_t)pcm[i]); fwrite(s, 1, 2, fp); }
    fclose(fp);
    free(pcm);
    return 1;
}
