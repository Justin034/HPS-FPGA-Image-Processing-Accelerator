/*
 * image.h - BMP I/O and the HPS preprocessing steps.
 *
 * Preprocessing (timed as part of both the software and FPGA paths):
 *   RGB -> gray:  Y = (77 R + 150 G + 29 B) >> 8
 *   pad:          width/height up to multiples of 8, replicating the last row/column
 *   block order:  block 0's 64 pixels, then block 1's, ... (blocks in raster order,
 *                 row-major inside each block)
 *
 * All images are stored top row first, no row padding.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define IMAGE_MAX_DIM 16384

typedef struct {
    int width, height;
    uint8_t *data;              /* R, G, B per pixel */
} rgb_image_t;

typedef struct {
    int width, height;
    uint8_t *data;              /* one byte per pixel */
} gray_image_t;

/* Load a 24-bit uncompressed BMP (bottom-up or top-down). 0 on success, -1 on error
   (message printed to stderr). */
int bmp_load_rgb(const char *path, rgb_image_t *img);

/* Save a grayscale image as a 24-bit BMP (R = G = B). 0 on success, -1 on error. */
int bmp_save_gray(const char *path, const gray_image_t *img);

int  gray_alloc(gray_image_t *img, int width, int height);
void gray_free(gray_image_t *img);
void rgb_free(rgb_image_t *img);

/* Individual steps (allocate their output) */
int gray_from_rgb(const rgb_image_t *rgb, gray_image_t *gray);
int gray_pad8(const gray_image_t *in, gray_image_t *out);
int gray_crop(const gray_image_t *in, int width, int height, gray_image_t *out);

size_t gray_block_count(const gray_image_t *padded);
void gray_to_blocks(const gray_image_t *padded, uint8_t *blocks);
void blocks_to_gray(const uint8_t *blocks, gray_image_t *padded);

/*
 * Preprocessing split into allocate (once) and run (timed):
 *   preprocess_alloc: allocates gray (image size), padded (multiple of 8) and the
 *                     block buffer (nblocks * 64 bytes).
 *   preprocess_run:   gray -> pad -> block order, no allocation.
 */
int  preprocess_alloc(const rgb_image_t *rgb, gray_image_t *gray, gray_image_t *padded,
                      uint8_t **blocks, size_t *nblocks);
void preprocess_run(const rgb_image_t *rgb, gray_image_t *gray, gray_image_t *padded,
                    uint8_t *blocks);

#endif /* IMAGE_H */
