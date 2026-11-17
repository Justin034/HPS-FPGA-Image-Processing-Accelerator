/*
 * dct_ref.h - bit-exact reference of the FPGA DCT + quantizer.
 *
 * This is the single source of truth for the arithmetic. It is used as:
 *   - the software baseline on the HPS (--mode sw)
 *   - the checker for every FPGA result on the board
 *   - the reference in Verilator simulation (through DPI-C)
 * If the hardware arithmetic changes, this file and dct_ref.c change with it.
 *
 * Arithmetic (see HPS/DESIGN.md):
 *   X  = pixel - 128
 *   T  = (X . C^T + 2^14) >> 15              pass 1 (rows)
 *   D  = (C . T   + 2^14) >> 15              pass 2 (columns)
 *   Qs = clamp(JPEG_LUMA * scale, 1, 255)
 *   R  = round(65536 / Qs)                   17-bit reciprocal
 *   q  = sign(D) * ((|D| * R + 2^15) >> 16)
 * ">>" is an arithmetic (floor) shift, like SystemVerilog ">>>".
 */
#ifndef DCT_REF_H
#define DCT_REF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {      /* also compiled into the Verilator (C++) testbench */
#endif

#define DCT_N            8
#define DCT_BLOCK        64
#define DCT_FRAC_BITS    15
#define DCT_PASS_ROUND   (1 << (DCT_FRAC_BITS - 1))   /* 2^14 */
#define DCT_RECIP_BITS   16
#define DCT_RECIP_ROUND  (1 << (DCT_RECIP_BITS - 1))  /* 2^15 */
#define DCT_RECIP_MAX    65536                        /* R for Qs = 1 */

/* Standard JPEG luminance quantization table, row-major. */
extern const uint8_t JPEG_LUMA[DCT_BLOCK];

/* 8x8 DCT-II matrix in Q1.15, row-major: DCT_Q15[u*8 + x] = C[u][x]. */
extern const int16_t DCT_Q15[DCT_BLOCK];

/* Qs = clamp(JPEG_LUMA * scale, 1, 255). Returns 0, or -1 if scale < 1. */
int qtable_scaled(int scale, uint16_t qs[DCT_BLOCK]);

/* R = round(65536 / Qs), round half up. Returns 0, or -1 if any Qs is outside 1..255. */
int qtable_reciprocals(const uint16_t qs[DCT_BLOCK], uint32_t recip[DCT_BLOCK]);

/* Floor shift right (arithmetic shift), defined for negative values on every compiler. */
int32_t dct_asr(int64_t v, int shift);

/* q = sign(d) * ((|d| * r + 2^15) >> 16) */
int32_t dct_quantize(int32_t d, uint32_t r);

/*
 * One 8x8 block, row-major in and out.
 *   pixels : 64 uint8 pixels
 *   recip  : 64 reciprocals (from qtable_reciprocals)
 *   q      : 64 quantized coefficients (output)
 *   t, d   : optional pass-1 / pass-2 results for debugging (may be NULL)
 */
void dct_ref_block(const uint8_t pixels[DCT_BLOCK], const uint32_t recip[DCT_BLOCK],
                   int16_t q[DCT_BLOCK], int32_t t[DCT_BLOCK], int32_t d[DCT_BLOCK]);

/* Many blocks, stored one after another (block order). This is the software baseline. */
void dct_ref_blocks(const uint8_t *pixels, size_t nblocks,
                    const uint32_t recip[DCT_BLOCK], int16_t *q);

#ifdef __cplusplus
}
#endif

#endif /* DCT_REF_H */
