/*
 * dct_ref.c - bit-exact reference of the FPGA DCT + quantizer.
 * See dct_ref.h for the arithmetic and HPS/DESIGN.md for the reasoning.
 */
#include "dct_ref.h"

#include <string.h>

const uint8_t JPEG_LUMA[DCT_BLOCK] = {
    16, 11, 10, 16,  24,  40,  51,  61,
    12, 12, 14, 19,  26,  58,  60,  55,
    14, 13, 16, 24,  40,  57,  69,  56,
    14, 17, 22, 29,  51,  87,  80,  62,
    18, 22, 37, 56,  68, 109, 103,  77,
    24, 35, 55, 64,  81, 104, 113,  92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103,  99,
};

/*
 * C[u][x] = round( alpha(u) * cos((2x+1) u pi / 16) * 32768 )
 * alpha(0) = 1/sqrt(8), alpha(u>0) = 1/2.
 * Written out so the RTL ROM can copy the same 64 integers.
 * ref_selftest.c checks them against the formula.
 */
const int16_t DCT_Q15[DCT_BLOCK] = {
    11585,  11585,  11585,  11585,  11585,  11585,  11585,  11585,
    16069,  13623,   9102,   3196,  -3196,  -9102, -13623, -16069,
    15137,   6270,  -6270, -15137, -15137,  -6270,   6270,  15137,
    13623,  -3196, -16069,  -9102,   9102,  16069,   3196, -13623,
    11585, -11585, -11585,  11585,  11585, -11585, -11585,  11585,
     9102, -16069,   3196,  13623, -13623,  -3196,  16069,  -9102,
     6270, -15137,  15137,  -6270,  -6270,  15137, -15137,   6270,
     3196,  -9102,  13623, -16069,  16069, -13623,   9102,  -3196,
};

int qtable_scaled(int scale, uint16_t qs[DCT_BLOCK])
{
    if (scale < 1)
        return -1;
    for (int i = 0; i < DCT_BLOCK; i++) {
        long v = (long)JPEG_LUMA[i] * scale;
        if (v < 1)
            v = 1;
        if (v > 255)
            v = 255;
        qs[i] = (uint16_t)v;
    }
    return 0;
}

int qtable_reciprocals(const uint16_t qs[DCT_BLOCK], uint32_t recip[DCT_BLOCK])
{
    for (int i = 0; i < DCT_BLOCK; i++)
        if (qs[i] < 1 || qs[i] > 255)
            return -1;
    for (int i = 0; i < DCT_BLOCK; i++)
        recip[i] = (DCT_RECIP_MAX + qs[i] / 2u) / qs[i];
    return 0;
}

int32_t dct_asr(int64_t v, int shift)
{
    /* Floor division by 2^shift. Plain ">>" on a negative value is
       implementation-defined in C (GCC does an arithmetic shift, but this
       makes the behaviour explicit and portable). */
    if (v >= 0)
        return (int32_t)(v >> shift);
    return (int32_t)-((-v + ((int64_t)1 << shift) - 1) >> shift);
}

int32_t dct_quantize(int32_t d, uint32_t r)
{
    uint64_t mag = (uint64_t)(d < 0 ? -(int64_t)d : (int64_t)d);
    mag = (mag * r + DCT_RECIP_ROUND) >> DCT_RECIP_BITS;
    return d < 0 ? -(int32_t)mag : (int32_t)mag;
}

void dct_ref_block(const uint8_t pixels[DCT_BLOCK], const uint32_t recip[DCT_BLOCK],
                   int16_t q[DCT_BLOCK], int32_t t_out[DCT_BLOCK], int32_t d_out[DCT_BLOCK])
{
    int32_t x[DCT_BLOCK], t[DCT_BLOCK], d[DCT_BLOCK];

    /* Level shift: 0..255 -> -128..127 */
    for (int i = 0; i < DCT_BLOCK; i++)
        x[i] = (int32_t)pixels[i] - 128;

    /* Pass 1 (rows): T[r][u] = sum_k X[r][k] * C[u][k]    (T = X . C^T) */
    for (int r = 0; r < DCT_N; r++) {
        for (int u = 0; u < DCT_N; u++) {
            int64_t acc = 0;
            for (int k = 0; k < DCT_N; k++)
                acc += (int64_t)x[r * DCT_N + k] * DCT_Q15[u * DCT_N + k];
            t[r * DCT_N + u] = dct_asr(acc + DCT_PASS_ROUND, DCT_FRAC_BITS);
        }
    }

    /* Pass 2 (columns): D[v][u] = sum_k C[v][k] * T[k][u]   (D = C . T) */
    for (int v = 0; v < DCT_N; v++) {
        for (int u = 0; u < DCT_N; u++) {
            int64_t acc = 0;
            for (int k = 0; k < DCT_N; k++)
                acc += (int64_t)DCT_Q15[v * DCT_N + k] * t[k * DCT_N + u];
            d[v * DCT_N + u] = dct_asr(acc + DCT_PASS_ROUND, DCT_FRAC_BITS);
        }
    }

    /* Quantize */
    for (int i = 0; i < DCT_BLOCK; i++)
        q[i] = (int16_t)dct_quantize(d[i], recip[i]);

    if (t_out)
        memcpy(t_out, t, sizeof t);
    if (d_out)
        memcpy(d_out, d, sizeof d);
}

void dct_ref_blocks(const uint8_t *pixels, size_t nblocks,
                    const uint32_t recip[DCT_BLOCK], int16_t *q)
{
    for (size_t b = 0; b < nblocks; b++)
        dct_ref_block(pixels + b * DCT_BLOCK, recip, q + b * DCT_BLOCK, NULL, NULL);
}
