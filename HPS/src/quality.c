/*
 * quality.c - reconstruction (dequantize + inverse DCT) and PSNR. See quality.h.
 */
#include "quality.h"

#include <math.h>

static const double PI = 3.14159265358979323846;

void dct_float_matrix(double c[64])
{
    for (int u = 0; u < 8; u++) {
        double a = (u == 0) ? sqrt(1.0 / 8.0) : 0.5;
        for (int x = 0; x < 8; x++)
            c[u * 8 + x] = a * cos((2 * x + 1) * u * PI / 16.0);
    }
}

/* The matrix is computed once and reused. */
static const double *matrix(void)
{
    static double c[64];
    static int ready = 0;
    if (!ready) {
        dct_float_matrix(c);
        ready = 1;
    }
    return c;
}

void dct_float_block(const uint8_t pixels[64], double d[64])
{
    const double *c = matrix();
    double tmp[64];
    /* tmp = (X - 128) . C^T   -> tmp[r][u] = sum_k X[r][k] * C[u][k] */
    for (int r = 0; r < 8; r++)
        for (int u = 0; u < 8; u++) {
            double s = 0;
            for (int k = 0; k < 8; k++)
                s += ((double)pixels[r * 8 + k] - 128.0) * c[u * 8 + k];
            tmp[r * 8 + u] = s;
        }
    /* D = C . tmp   -> D[v][u] = sum_k C[v][k] * tmp[k][u] */
    for (int v = 0; v < 8; v++)
        for (int u = 0; u < 8; u++) {
            double s = 0;
            for (int k = 0; k < 8; k++)
                s += c[v * 8 + k] * tmp[k * 8 + u];
            d[v * 8 + u] = s;
        }
}

void idct_block(const int16_t q[64], const uint16_t qs[64], uint8_t out[64])
{
    const double *c = matrix();
    double dq[64], tmp[64];
    for (int i = 0; i < 64; i++)
        dq[i] = (double)q[i] * qs[i];                 /* dequantize */
    /* tmp = C^T . D'   -> tmp[y][u] = sum_v C[v][y] * D'[v][u] */
    for (int y = 0; y < 8; y++)
        for (int u = 0; u < 8; u++) {
            double s = 0;
            for (int v = 0; v < 8; v++)
                s += c[v * 8 + y] * dq[v * 8 + u];
            tmp[y * 8 + u] = s;
        }
    /* X = tmp . C   -> X[y][x] = sum_u tmp[y][u] * C[u][x] */
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            double s = 128.0;                         /* undo level shift */
            for (int u = 0; u < 8; u++)
                s += tmp[y * 8 + u] * c[u * 8 + x];
            long v = (long)floor(s + 0.5);
            out[y * 8 + x] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
}

void reconstruct_blocks(const int16_t *q, size_t nblocks, const uint16_t qs[64], uint8_t *out)
{
    for (size_t b = 0; b < nblocks; b++)
        idct_block(q + b * 64, qs, out + b * 64);
}

double gray_mse(const gray_image_t *a, const gray_image_t *b, int width, int height)
{
    double sum = 0;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            double d = (double)a->data[(size_t)y * (size_t)a->width + (size_t)x]
                     - (double)b->data[(size_t)y * (size_t)b->width + (size_t)x];
            sum += d * d;
        }
    return sum / ((double)width * (double)height);
}

double psnr_from_mse(double mse)
{
    if (mse <= 0)
        return INFINITY;
    return 10.0 * log10(255.0 * 255.0 / mse);
}
