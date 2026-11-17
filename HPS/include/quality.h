/*
 * quality.h - reconstruction (dequantize + inverse DCT) and PSNR.
 *
 * The inverse DCT is floating point on purpose: the decoder is not what is being
 * measured, so the most accurate inverse makes PSNR reflect only the loss of the
 * FPGA's DCT + quantization.
 */
#ifndef QUALITY_H
#define QUALITY_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

/* Exact DCT-II matrix: c[u*8 + x] = alpha(u) * cos((2x+1) u pi / 16). */
void dct_float_matrix(double c[64]);

/* Floating-point forward DCT of one block (with level shift): D = C . (X-128) . C^T */
void dct_float_block(const uint8_t pixels[64], double d[64]);

/* Dequantize (q * Qs), inverse DCT, +128, round, clamp to 0..255. */
void idct_block(const int16_t q[64], const uint16_t qs[64], uint8_t out[64]);

/* Same for many blocks in block order. */
void reconstruct_blocks(const int16_t *q, size_t nblocks, const uint16_t qs[64], uint8_t *out);

/* Mean squared error over the top-left width x height region of a and b. */
double gray_mse(const gray_image_t *a, const gray_image_t *b, int width, int height);

/* PSNR in dB for 8-bit images; returns INFINITY when mse == 0. */
double psnr_from_mse(double mse);

#endif /* QUALITY_H */
