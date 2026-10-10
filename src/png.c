#include "png.h"

static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc_init(void) {
    uint32_t n, k, c;
    for (n = 0; n < 256; ++n) {
        c = n;
        for (k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put_chunk(FILE *fp, const char *type, const uint8_t *data, size_t n) {
    uint8_t b[4];
    uint32_t c;
    wr32be(b, (uint32_t)n);
    fwrite(b, 1, 4, fp);
    fwrite(type, 1, 4, fp);
    if (n) fwrite(data, 1, n, fp);
    c = crc_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (n) c = crc_update(c, data, n);
    wr32be(b, c ^ 0xFFFFFFFFu);
    fwrite(b, 1, 4, fp);
}

int png_write_rgba(const char *path, int w, int h, const uint8_t *rgba) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    FILE *fp;
    uint8_t ihdr[13];
    size_t row = (size_t)w * 4 + 1, raw_len = row * (size_t)h, nblocks, i, pos;
    uint8_t *raw, *z;
    size_t zlen;
    uint32_t a = 1, b = 0;

    if (!crc_ready) crc_init();
    fp = fopen(path, "wb");
    if (!fp) { fprintf(stderr, "Couldn't create %s\n", path); return 0; }
    fwrite(sig, 1, 8, fp);
    wr32be(ihdr, (uint32_t)w);
    wr32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    put_chunk(fp, "IHDR", ihdr, 13);

    /* filter byte 0 before every scanline */
    raw = xmalloc(raw_len);
    for (i = 0; i < (size_t)h; ++i) {
        raw[i * row] = 0;
        memcpy(raw + i * row + 1, rgba + i * (size_t)w * 4, (size_t)w * 4);
    }
    /* zlib stream of stored deflate blocks (max 65535 bytes each) */
    nblocks = (raw_len + 65534) / 65535;
    if (nblocks == 0) nblocks = 1;
    zlen = 2 + nblocks * 5 + raw_len + 4;
    z = xmalloc(zlen);
    z[0] = 0x78; z[1] = 0x01;
    pos = 2;
    for (i = 0; i < nblocks; ++i) {
        size_t off = i * 65535, len = raw_len - off;
        if (len > 65535) len = 65535;
        z[pos++] = (i == nblocks - 1) ? 1 : 0;
        z[pos++] = len & 0xff; z[pos++] = (len >> 8) & 0xff;
        z[pos++] = ~len & 0xff; z[pos++] = (~len >> 8) & 0xff;
        memcpy(z + pos, raw + off, len);
        pos += len;
    }
    for (i = 0; i < raw_len; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    wr32be(z + pos, (b << 16) | a);
    pos += 4;
    put_chunk(fp, "IDAT", z, pos);
    put_chunk(fp, "IEND", NULL, 0);
    fclose(fp);
    free(raw);
    free(z);
    return 1;
}
