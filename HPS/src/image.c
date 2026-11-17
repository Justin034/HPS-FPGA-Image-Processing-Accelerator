/*
 * image.c - BMP I/O and the HPS preprocessing steps. See image.h.
 * Replaces the old data_setup.c / data_input.h loader.
 */
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little-endian helpers (BMP is little-endian) ---- */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    uint8_t *buf = NULL;
    long len;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: cannot determine file size\n", path);
        fclose(f);
        return NULL;
    }
    buf = malloc(len > 0 ? (size_t)len : 1);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "%s: read failed\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)len;
    return buf;
}

/* ---- BMP ---- */

int bmp_load_rgb(const char *path, rgb_image_t *img)
{
    size_t size;
    uint8_t *f = read_file(path, &size);
    img->width = img->height = 0;
    img->data = NULL;
    if (!f)
        return -1;

    int rc = -1;
    if (size < 54 || f[0] != 'B' || f[1] != 'M') {
        fprintf(stderr, "%s: not a BMP file\n", path);
        goto out;
    }
    uint32_t data_off = rd32(f + 10);
    uint32_t dib_size = rd32(f + 14);
    int32_t  w        = (int32_t)rd32(f + 18);
    int32_t  h_raw    = (int32_t)rd32(f + 22);
    uint32_t planes   = rd16(f + 26);
    uint32_t bpp      = rd16(f + 28);
    uint32_t comp     = rd32(f + 30);

    if (dib_size < 40 || planes != 1 || bpp != 24 || comp != 0) {
        fprintf(stderr, "%s: only 24-bit uncompressed BMPs are supported "
                        "(bpp %u, compression %u)\n", path, (unsigned)bpp, (unsigned)comp);
        goto out;
    }
    int top_down = h_raw < 0;
    int32_t h = top_down ? -h_raw : h_raw;
    if (w <= 0 || h <= 0 || w > IMAGE_MAX_DIM || h > IMAGE_MAX_DIM) {
        fprintf(stderr, "%s: unsupported size %dx%d\n", path, (int)w, (int)h);
        goto out;
    }
    size_t row = ((size_t)w * 3 + 3) & ~(size_t)3;     /* rows padded to 4 bytes */
    if (data_off > size || row * (size_t)h > size - data_off) {
        fprintf(stderr, "%s: file is truncated\n", path);
        goto out;
    }

    img->data = malloc((size_t)w * (size_t)h * 3);
    if (!img->data) {
        fprintf(stderr, "%s: out of memory\n", path);
        goto out;
    }
    img->width = w;
    img->height = h;
    for (int32_t y = 0; y < h; y++) {
        /* bottom-up files store the last image row first */
        const uint8_t *src = f + data_off + row * (size_t)(top_down ? y : h - 1 - y);
        uint8_t *dst = img->data + (size_t)y * (size_t)w * 3;
        for (int32_t x = 0; x < w; x++) {
            dst[3 * x + 0] = src[3 * x + 2];   /* BMP stores B, G, R */
            dst[3 * x + 1] = src[3 * x + 1];
            dst[3 * x + 2] = src[3 * x + 0];
        }
    }
    rc = 0;
out:
    free(f);
    return rc;
}

int bmp_save_gray(const char *path, const gray_image_t *img)
{
    size_t row = ((size_t)img->width * 3 + 3) & ~(size_t)3;
    size_t data_size = row * (size_t)img->height;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    wr32(hdr + 2, (uint32_t)(54 + data_size));
    wr32(hdr + 10, 54);                        /* pixel data offset */
    wr32(hdr + 14, 40);                        /* BITMAPINFOHEADER */
    wr32(hdr + 18, (uint32_t)img->width);
    wr32(hdr + 22, (uint32_t)img->height);     /* positive: bottom-up */
    wr16(hdr + 26, 1);                         /* planes */
    wr16(hdr + 28, 24);                        /* bits per pixel */
    wr32(hdr + 34, (uint32_t)data_size);
    wr32(hdr + 38, 2835);                      /* 72 dpi */
    wr32(hdr + 42, 2835);

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    uint8_t *line = calloc(row, 1);
    int ok = line && fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr;
    for (int y = img->height - 1; ok && y >= 0; y--) {
        const uint8_t *src = img->data + (size_t)y * (size_t)img->width;
        for (int x = 0; x < img->width; x++)
            line[3 * x] = line[3 * x + 1] = line[3 * x + 2] = src[x];
        ok = fwrite(line, 1, row, f) == row;
    }
    free(line);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        fprintf(stderr, "%s: write failed\n", path);
    return ok ? 0 : -1;
}

/* ---- allocation ---- */

int gray_alloc(gray_image_t *img, int width, int height)
{
    img->width = width;
    img->height = height;
    img->data = malloc((size_t)width * (size_t)height);
    if (!img->data) {
        fprintf(stderr, "out of memory (%dx%d image)\n", width, height);
        return -1;
    }
    return 0;
}

void gray_free(gray_image_t *img)
{
    free(img->data);
    img->data = NULL;
    img->width = img->height = 0;
}

void rgb_free(rgb_image_t *img)
{
    free(img->data);
    img->data = NULL;
    img->width = img->height = 0;
}

/* ---- preprocessing steps ---- */

static void gray_from_rgb_into(const rgb_image_t *rgb, gray_image_t *gray)
{
    size_t n = (size_t)rgb->width * (size_t)rgb->height;
    const uint8_t *p = rgb->data;
    for (size_t i = 0; i < n; i++, p += 3)
        gray->data[i] = (uint8_t)((77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8);
}

static void pad8_into(const gray_image_t *in, gray_image_t *out)
{
    for (int y = 0; y < out->height; y++) {
        int sy = y < in->height ? y : in->height - 1;
        const uint8_t *src = in->data + (size_t)sy * (size_t)in->width;
        uint8_t *dst = out->data + (size_t)y * (size_t)out->width;
        memcpy(dst, src, (size_t)in->width);
        for (int x = in->width; x < out->width; x++)
            dst[x] = src[in->width - 1];
    }
}

int gray_from_rgb(const rgb_image_t *rgb, gray_image_t *gray)
{
    if (gray_alloc(gray, rgb->width, rgb->height) != 0)
        return -1;
    gray_from_rgb_into(rgb, gray);
    return 0;
}

int gray_pad8(const gray_image_t *in, gray_image_t *out)
{
    if (gray_alloc(out, (in->width + 7) & ~7, (in->height + 7) & ~7) != 0)
        return -1;
    pad8_into(in, out);
    return 0;
}

int gray_crop(const gray_image_t *in, int width, int height, gray_image_t *out)
{
    if (width > in->width || height > in->height)
        return -1;
    if (gray_alloc(out, width, height) != 0)
        return -1;
    for (int y = 0; y < height; y++)
        memcpy(out->data + (size_t)y * (size_t)width,
               in->data + (size_t)y * (size_t)in->width, (size_t)width);
    return 0;
}

size_t gray_block_count(const gray_image_t *padded)
{
    return (size_t)(padded->width / 8) * (size_t)(padded->height / 8);
}

void gray_to_blocks(const gray_image_t *padded, uint8_t *blocks)
{
    int nbx = padded->width / 8, nby = padded->height / 8;
    for (int by = 0; by < nby; by++)
        for (int bx = 0; bx < nbx; bx++) {
            uint8_t *dst = blocks + ((size_t)by * (size_t)nbx + (size_t)bx) * 64;
            for (int y = 0; y < 8; y++)
                memcpy(dst + y * 8,
                       padded->data + (size_t)(by * 8 + y) * (size_t)padded->width + (size_t)bx * 8, 8);
        }
}

void blocks_to_gray(const uint8_t *blocks, gray_image_t *padded)
{
    int nbx = padded->width / 8, nby = padded->height / 8;
    for (int by = 0; by < nby; by++)
        for (int bx = 0; bx < nbx; bx++) {
            const uint8_t *src = blocks + ((size_t)by * (size_t)nbx + (size_t)bx) * 64;
            for (int y = 0; y < 8; y++)
                memcpy(padded->data + (size_t)(by * 8 + y) * (size_t)padded->width + (size_t)bx * 8,
                       src + y * 8, 8);
        }
}

int preprocess_alloc(const rgb_image_t *rgb, gray_image_t *gray, gray_image_t *padded,
                     uint8_t **blocks, size_t *nblocks)
{
    gray->data = padded->data = NULL;
    *blocks = NULL;
    if (gray_alloc(gray, rgb->width, rgb->height) != 0)
        return -1;
    if (gray_alloc(padded, (rgb->width + 7) & ~7, (rgb->height + 7) & ~7) != 0) {
        gray_free(gray);
        return -1;
    }
    *nblocks = gray_block_count(padded);
    *blocks = malloc(*nblocks * 64);
    if (!*blocks) {
        fprintf(stderr, "out of memory (block buffer)\n");
        gray_free(gray);
        gray_free(padded);
        return -1;
    }
    return 0;
}

void preprocess_run(const rgb_image_t *rgb, gray_image_t *gray, gray_image_t *padded,
                    uint8_t *blocks)
{
    gray_from_rgb_into(rgb, gray);
    pad8_into(gray, padded);
    gray_to_blocks(padded, blocks);
}
