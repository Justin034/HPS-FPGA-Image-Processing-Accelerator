/*
 * test_ref.c - native PC test of the C reference library (phase 1).
 *
 *   test_ref [--log DIR] [--tmp DIR] [IMAGE.bmp [--recon-prefix PREFIX]]
 *
 * 1. Runs the reference self-test and logs the result to DIR/tests.{md,csv}.
 * 2. With an image: preprocesses it, encodes it at Q scales x1/x2/x4/x8 with the
 *    reference, reconstructs it, prints MSE/PSNR and logs each scale to
 *    DIR/quality.{md,csv}. With --recon-prefix, saves PREFIX_<name>_s<scale>.bmp.
 *
 * Exit code: 0 if everything passed, 1 otherwise.
 */
#include "dct_ref.h"
#include "image.h"
#include "quality.h"
#include "ref_selftest.h"
#include "reslog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int image_quality(const char *path, const char *recon_prefix)
{
    rgb_image_t rgb;
    gray_image_t gray, padded, rec_padded, rec;
    uint8_t *blocks;
    size_t n;

    if (bmp_load_rgb(path, &rgb) != 0)
        return 1;
    if (preprocess_alloc(&rgb, &gray, &padded, &blocks, &n) != 0) {
        rgb_free(&rgb);
        return 1;
    }
    preprocess_run(&rgb, &gray, &padded, blocks);

    int16_t *q = malloc(n * 64 * sizeof *q);
    uint8_t *out = malloc(n * 64);
    if (!q || !out || gray_alloc(&rec_padded, padded.width, padded.height) != 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    char size[32];
    snprintf(size, sizeof size, "%dx%d", gray.width, gray.height);
    printf("\n%s: %s (padded %dx%d), %zu blocks\n", path, size, padded.width, padded.height, n);
    printf("  scale      MSE   PSNR dB\n");

    const int scales[] = {1, 2, 4, 8};
    int rc = 0;
    for (int s = 0; s < 4; s++) {
        uint16_t qs[64];
        uint32_t r[64];
        qtable_scaled(scales[s], qs);
        qtable_reciprocals(qs, r);
        dct_ref_blocks(blocks, n, r, q);
        reconstruct_blocks(q, n, qs, out);
        blocks_to_gray(out, &rec_padded);

        double mse = gray_mse(&gray, &rec_padded, gray.width, gray.height);
        double psnr = psnr_from_mse(mse);
        printf("  x%-5d %8.3f  %8.2f\n", scales[s], mse, psnr);

        char sc[8], ms[32], ps[32];
        snprintf(sc, sizeof sc, "%d", scales[s]);
        snprintf(ms, sizeof ms, "%.3f", mse);
        if (isinf(psnr))
            snprintf(ps, sizeof ps, "inf");
        else
            snprintf(ps, sizeof ps, "%.2f", psnr);
        const char *row[] = { reslog_timestamp(), reslog_host(), base_name(path), size,
                              "sw (reference)", sc, ms, ps };
        reslog_append(&RESLOG_QUALITY, row);

        if (recon_prefix) {
            char out_path[600];
            char stem[256];
            snprintf(stem, sizeof stem, "%s", base_name(path));
            char *dot = strrchr(stem, '.');
            if (dot)
                *dot = '\0';
            snprintf(out_path, sizeof out_path, "%s_%s_s%d.bmp", recon_prefix, stem, scales[s]);
            if (gray_crop(&rec_padded, gray.width, gray.height, &rec) == 0) {
                if (bmp_save_gray(out_path, &rec) == 0)
                    printf("         -> %s\n", out_path);
                else
                    rc = 1;
                gray_free(&rec);
            }
        }
    }

    free(q);
    free(out);
    free(blocks);
    gray_free(&gray);
    gray_free(&padded);
    gray_free(&rec_padded);
    rgb_free(&rgb);
    return rc;
}

int main(int argc, char **argv)
{
    const char *log_dir = NULL, *tmp_dir = ".", *image = NULL, *recon_prefix = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--log") && i + 1 < argc)
            log_dir = argv[++i];
        else if (!strcmp(argv[i], "--tmp") && i + 1 < argc)
            tmp_dir = argv[++i];
        else if (!strcmp(argv[i], "--recon-prefix") && i + 1 < argc)
            recon_prefix = argv[++i];
        else if (argv[i][0] == '-') {
            fprintf(stderr, "usage: %s [--log DIR] [--tmp DIR] [IMAGE.bmp [--recon-prefix PREFIX]]\n",
                    argv[0]);
            return 2;
        } else
            image = argv[i];
    }
    reslog_set_dir(log_dir);

    char summary[128];
    int failures = ref_selftest(tmp_dir, summary, sizeof summary);
    const char *row[] = { reslog_timestamp(), reslog_host(), "C reference self-test (test_ref)",
                          "sw", "-", failures ? "FAIL" : "PASS", summary };
    reslog_append(&RESLOG_TESTS, row);

    int rc = failures ? 1 : 0;
    if (image && image_quality(image, recon_prefix) != 0)
        rc = 1;

    if (log_dir && log_dir[0])
        printf("\nResults appended to %s/\n", log_dir);
    return rc;
}
