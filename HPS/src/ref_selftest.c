/*
 * ref_selftest.c - checks the C reference against floating point and itself.
 * See ref_selftest.h and HPS/DESIGN.md (section "Self-test").
 */
#include "ref_selftest.h"

#include "dct_ref.h"
#include "image.h"
#include "quality.h"
#include "test_blocks.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SELFTEST_RANDOM_BLOCKS 2000

static int g_pass, g_fail;

static void check(const char *name, int ok, const char *detail)
{
    printf("[%s] %s", ok ? "PASS" : "FAIL", name);
    if (detail && detail[0])
        printf("  (%s)", detail);
    printf("\n");
    if (ok)
        g_pass++;
    else
        g_fail++;
}

static long round_half_away(double v)
{
    return v < 0 ? -(long)floor(-v + 0.5) : (long)floor(v + 0.5);
}

/* 1. constants match the formula */
static void check_matrix_formula(void)
{
    double c[64];
    int bad = 0;
    dct_float_matrix(c);
    for (int i = 0; i < 64; i++)
        if (round_half_away(c[i] * 32768.0) != DCT_Q15[i])
            bad++;
    char d[64];
    snprintf(d, sizeof d, "%d entries differ", bad);
    check("DCT_Q15 matches the cosine formula", bad == 0, bad ? d : "");
}

/* 2. orthogonal: C . C^T ~ 2^30 I */
static void check_orthogonal(void)
{
    int64_t max_off = 0, max_diag = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            int64_t s = 0;
            for (int k = 0; k < 8; k++)
                s += (int64_t)DCT_Q15[i * 8 + k] * DCT_Q15[j * 8 + k];
            int64_t e = (i == j) ? llabs(s - ((int64_t)1 << 30)) : llabs(s);
            if (i == j && e > max_diag)
                max_diag = e;
            if (i != j && e > max_off)
                max_off = e;
        }
    const int64_t tol = ((int64_t)1 << 30) / 10000;   /* 1e-4 relative */
    char d[96];
    snprintf(d, sizeof d, "max off-diagonal %lld, max diagonal error %lld",
             (long long)max_off, (long long)max_diag);
    check("DCT_Q15 is orthogonal within rounding", max_off < tol && max_diag < tol, d);
}

/* 3. reciprocals fit 17 bits, scaled tables are valid */
static void check_reciprocals(void)
{
    uint32_t rmin = UINT32_MAX, rmax = 0;
    for (unsigned qs = 1; qs <= 255; qs++) {
        uint16_t t[64];
        uint32_t r[64];
        for (int i = 0; i < 64; i++)
            t[i] = (uint16_t)qs;
        qtable_reciprocals(t, r);
        if (r[0] < rmin) rmin = r[0];
        if (r[0] > rmax) rmax = r[0];
    }
    int tables_ok = 1;
    const int scales[] = {1, 2, 4, 8};
    for (int s = 0; s < 4; s++) {
        uint16_t qs[64];
        uint32_t r[64];
        if (qtable_scaled(scales[s], qs) != 0 || qtable_reciprocals(qs, r) != 0)
            tables_ok = 0;
    }
    char d[64];
    snprintf(d, sizeof d, "R from %u to %u", (unsigned)rmin, (unsigned)rmax);
    check("Reciprocals fit 17 bits; scaled tables x1/x2/x4/x8 valid",
          rmax == DCT_RECIP_MAX && rmin >= 1 && tables_ok, d);
}

/* 4 + 5. known blocks */
static void check_known_blocks(void)
{
    uint16_t qs[64];
    uint32_t r[64];
    uint8_t px[64];
    int16_t q[64];
    int32_t d[64];
    qtable_scaled(1, qs);
    qtable_reciprocals(qs, r);

    memset(px, 128, sizeof px);
    dct_ref_block(px, r, q, NULL, d);
    int zero = 1;
    for (int i = 0; i < 64; i++)
        if (q[i] || d[i])
            zero = 0;
    check("All-128 block gives all zeros", zero, "");

    memset(px, 255, sizeof px);
    dct_ref_block(px, r, q, NULL, d);
    int ac_zero = 1;
    for (int i = 1; i < 64; i++)
        if (q[i])
            ac_zero = 0;
    char det[64];
    snprintf(det, sizeof det, "D(DC) = %d, q(DC) = %d", (int)d[0], (int)q[0]);
    check("Constant-255 block: DC ~ 1016, q(DC) 63..64, AC = 0",
          abs(d[0] - 1016) <= 2 && (q[0] == 63 || q[0] == 64) && ac_zero, det);
}

/* 6, 7, 8. fixed point vs floating point over the test-block set */
static void check_against_float(double *max_err_out)
{
    const int n = test_block_count(SELFTEST_RANDOM_BLOCKS);
    const int scales[] = {1, 2, 4, 8};
    uint16_t qs[4][64];
    uint32_t r[4][64];
    for (int s = 0; s < 4; s++) {
        qtable_scaled(scales[s], qs[s]);
        qtable_reciprocals(qs[s], r[s]);
    }

    double max_err = 0;
    int32_t max_t = 0, max_d = 0;
    long max_qdiff = 0;
    for (int b = 0; b < n; b++) {
        uint8_t px[64];
        int16_t q[64];
        int32_t t[64], d[64];
        double df[64];
        test_block(b, px);
        dct_float_block(px, df);
        for (int s = 0; s < 4; s++) {
            dct_ref_block(px, r[s], q, t, d);
            for (int i = 0; i < 64; i++) {
                if (s == 0) {
                    double e = fabs(d[i] - df[i]);
                    if (e > max_err) max_err = e;
                    if (abs(t[i]) > max_t) max_t = abs(t[i]);
                    if (abs(d[i]) > max_d) max_d = abs(d[i]);
                }
                long qf = round_half_away(df[i] / qs[s][i]);
                long diff = labs((long)q[i] - qf);
                if (diff > max_qdiff) max_qdiff = diff;
            }
        }
    }
    char det[96];
    snprintf(det, sizeof det, "%d blocks, max error %.3f", n, max_err);
    check("Fixed-point DCT within 2.5 of floating point", max_err <= 2.5, det);
    snprintf(det, sizeof det, "max |T| = %d, max |D| = %d", (int)max_t, (int)max_d);
    check("Pass-1 and pass-2 values fit int16", max_t < 32768 && max_d < 32768, det);
    snprintf(det, sizeof det, "max difference %ld", max_qdiff);
    check("Quantized output within 1 of float quantization (all scales)", max_qdiff <= 1, det);
    *max_err_out = max_err;
}

/* 9. reciprocal quantizer vs exact rounded division (documented limitation) */
static void check_recip_vs_division(void)
{
    long mismatches = 0, total = 0, worst = 0;
    for (int qs = 1; qs <= 255; qs++) {
        uint16_t t[64];
        uint32_t r[64];
        for (int i = 0; i < 64; i++)
            t[i] = (uint16_t)qs;
        qtable_reciprocals(t, r);
        for (int d = -2048; d <= 2048; d++) {
            long mag = (2L * labs(d) + qs) / (2L * qs);      /* round half away */
            long exact = d < 0 ? -mag : mag;
            long diff = labs((long)dct_quantize(d, r[0]) - exact);
            if (diff) mismatches++;
            if (diff > worst) worst = diff;
            total++;
        }
    }
    char det[96];
    snprintf(det, sizeof det, "%ld of %ld (D, Qs) pairs differ by 1", mismatches, total);
    check("Reciprocal quantizer within 1 of exact division", worst <= 1, det);
}

/* 10, 11. block order and padding */
static void check_image_steps(void)
{
    gray_image_t img, back, small, padded;
    gray_alloc(&img, 40, 24);
    for (int i = 0; i < 40 * 24; i++)
        img.data[i] = (uint8_t)(i * 37 + 11);
    uint8_t *blocks = malloc(gray_block_count(&img) * 64);
    gray_alloc(&back, 40, 24);
    gray_to_blocks(&img, blocks);
    blocks_to_gray(blocks, &back);
    check("Block order round trip", memcmp(img.data, back.data, 40 * 24) == 0, "");
    free(blocks);
    gray_free(&img);
    gray_free(&back);

    gray_alloc(&small, 13, 10);
    for (int i = 0; i < 13 * 10; i++)
        small.data[i] = (uint8_t)i;
    gray_pad8(&small, &padded);
    int ok = padded.width == 16 && padded.height == 16;
    for (int y = 0; ok && y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int sy = y < 10 ? y : 9, sx = x < 13 ? x : 12;
            if (padded.data[y * 16 + x] != small.data[sy * 13 + sx])
                ok = 0;
        }
    check("Padding to 16x16 replicates edges", ok, "");
    gray_free(&small);
    gray_free(&padded);
}

/* 12. reconstruction quality on a smooth image */
static void check_reconstruction(void)
{
    gray_image_t img, rec;
    gray_alloc(&img, 64, 64);
    gray_alloc(&rec, 64, 64);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            img.data[y * 64 + x] = (uint8_t)(30 + x * 2 + y);
    size_t n = gray_block_count(&img);
    uint8_t *blocks = malloc(n * 64), *out = malloc(n * 64);
    int16_t *q = malloc(n * 64 * sizeof *q);
    uint16_t qs[64];
    uint32_t r[64];
    qtable_scaled(1, qs);
    qtable_reciprocals(qs, r);
    gray_to_blocks(&img, blocks);
    dct_ref_blocks(blocks, n, r, q);
    reconstruct_blocks(q, n, qs, out);
    blocks_to_gray(out, &rec);
    double psnr = psnr_from_mse(gray_mse(&img, &rec, 64, 64));
    char det[64];
    snprintf(det, sizeof det, "PSNR %.2f dB", psnr);
    check("Encode + reconstruct smooth 64x64 image: PSNR > 30 dB at x1", psnr > 30.0, det);
    free(blocks);
    free(out);
    free(q);
    gray_free(&img);
    gray_free(&rec);
}

/* 13. BMP save / load round trip (odd width exercises row padding) */
static void check_bmp(const char *tmp_dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/selftest_tmp.bmp", tmp_dir ? tmp_dir : ".");
    gray_image_t img;
    rgb_image_t rgb;
    gray_alloc(&img, 13, 10);
    for (int i = 0; i < 13 * 10; i++)
        img.data[i] = (uint8_t)(i * 7);
    int ok = bmp_save_gray(path, &img) == 0 && bmp_load_rgb(path, &rgb) == 0;
    if (ok) {
        ok = rgb.width == 13 && rgb.height == 10;
        for (int i = 0; ok && i < 13 * 10; i++)
            if (rgb.data[3 * i] != img.data[i] || rgb.data[3 * i + 1] != img.data[i] ||
                rgb.data[3 * i + 2] != img.data[i])
                ok = 0;
        rgb_free(&rgb);
    }
    remove(path);
    gray_free(&img);
    check("BMP save / load round trip (13x10)", ok, ok ? "" : path);
}

int ref_selftest(const char *tmp_dir, char *summary, size_t summary_len)
{
    double max_err = 0;
    g_pass = g_fail = 0;

    check_matrix_formula();
    check_orthogonal();
    check_reciprocals();
    check_known_blocks();
    check_against_float(&max_err);
    check_recip_vs_division();
    check_image_steps();
    check_reconstruction();
    check_bmp(tmp_dir);

    printf("\n%s: %d/%d checks passed\n", g_fail ? "SELFTEST FAILED" : "SELFTEST PASSED",
           g_pass, g_pass + g_fail);
    if (summary && summary_len)
        snprintf(summary, summary_len, "%d/%d checks passed; max |D - float| %.3f",
                 g_pass, g_pass + g_fail, max_err);
    return g_fail;
}
