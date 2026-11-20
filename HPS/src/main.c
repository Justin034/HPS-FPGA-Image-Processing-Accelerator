/*
 * main.c - dctq: 8x8 DCT + quantization of a grayscale image, on the HPS in
 * software or on the FPGA, with timing, checking and automatic result logging.
 *
 *   dctq selftest                      C reference self-test (13 checks)
 *   dctq hwcheck                       FPGA link and accelerator checks
 *   dctq blocktest --mode M            all test blocks through mode M, checked
 *   dctq run IMG.bmp --mode M          timed image run, checked, PSNR
 *
 * Modes: sw  - software on the ARM (dct_ref.c, the baseline)
 *        reg - FPGA, option 1: register by register        (accelerator MODE 0)
 *        buf - FPGA, option 2: buffer inside the accelerator (MODE 1)
 *        dma - FPGA, options 3/4: mSGDMA                    (MODE 2; phase 9)
 *
 * Every command appends a row to <log dir>/{tests,runs,quality}.{md,csv}.
 * See HPS/DESIGN.md §8 and docs/testing.md.
 */
#define _POSIX_C_SOURCE 200809L

#include "accel_regs.h"
#include "dct_ref.h"
#include "fpga.h"
#include "image.h"
#include "quality.h"
#include "ref_selftest.h"
#include "reslog.h"
#include "test_blocks.h"
#include "timing.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MODE_SW, MODE_REG, MODE_BUF, MODE_DMA };
static const char *const MODE_NAMES[] = { "sw", "reg", "buf", "dma" };

typedef struct {
    const char   *cmd;
    const char   *image;
    const char   *recon;
    const char   *log_dir;
    const char   *tmp_dir;
    int           mode;
    int           scale;
    int           runs;
    int           nrand;
    unsigned long csr_phys, buf_phys;
} opts_t;

/* ========================================================================= */
/* Small helpers                                                             */
/* ========================================================================= */

static void usage(void)
{
    fprintf(stderr,
        "usage: dctq COMMAND [options]\n"
        "\n"
        "commands:\n"
        "  selftest                 C reference self-test\n"
        "  hwcheck                  FPGA link and accelerator checks (board, root)\n"
        "  blocktest --mode M       all test blocks through mode M, checked against the reference\n"
        "  run IMG.bmp --mode M     timed run on an image, checked, with PSNR\n"
        "\n"
        "options:\n"
        "  --mode M        sw | reg | buf | dma            (default sw)\n"
        "  --scale S       Q table scale 1, 2, 4 or 8       (default 1)\n"
        "  --runs N        timed runs after one warm-up     (default 10)\n"
        "  --recon F.bmp   save the reconstructed image (run)\n"
        "  --nrand N       random blocks after the 79 edge cases (blocktest, default %d)\n"
        "  --log DIR       result tables directory          (default results)\n"
        "  --no-log        do not write result tables\n"
        "  --tmp DIR       scratch directory for selftest   (default /tmp)\n"
        "  --csr ADDR      accelerator register address     (default 0x%08X)\n"
        "  --buf ADDR      accelerator buffer address       (default 0x%08X)\n",
        TEST_DEFAULT_RANDOM, ACCEL_CSR_PHYS, ACCEL_BUF_PHYS);
}

static int parse_int(const char *s, int lo, int hi, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = (int)v;
    return 0;
}

static int parse_addr(const char *s, unsigned long *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (*s == '\0' || *end != '\0' || (v & 3u) != 0)
        return -1;
    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, opts_t *o)
{
    memset(o, 0, sizeof *o);
    o->mode     = MODE_SW;
    o->scale    = 1;
    o->runs     = 10;
    o->nrand    = TEST_DEFAULT_RANDOM;
    o->log_dir  = "results";
    o->tmp_dir  = "/tmp";
    o->csr_phys = ACCEL_CSR_PHYS;
    o->buf_phys = ACCEL_BUF_PHYS;

    if (argc < 2)
        return -1;
    o->cmd = argv[1];

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--mode") && v) {
            int m;
            for (m = 0; m < 4; m++)
                if (!strcmp(v, MODE_NAMES[m]))
                    break;
            if (m == 4) {
                fprintf(stderr, "unknown mode '%s'\n", v);
                return -1;
            }
            o->mode = m;
            i++;
        } else if (!strcmp(a, "--scale") && v) {
            if (parse_int(v, 1, 8, &o->scale) || (o->scale & (o->scale - 1))) {
                fprintf(stderr, "--scale must be 1, 2, 4 or 8\n");
                return -1;
            }
            i++;
        } else if (!strcmp(a, "--runs") && v) {
            if (parse_int(v, 1, 1000, &o->runs)) {
                fprintf(stderr, "--runs must be 1..1000\n");
                return -1;
            }
            i++;
        } else if (!strcmp(a, "--nrand") && v) {
            if (parse_int(v, 0, 1000000, &o->nrand)) {
                fprintf(stderr, "--nrand must be 0..1000000\n");
                return -1;
            }
            i++;
        } else if (!strcmp(a, "--recon") && v) {
            o->recon = v;
            i++;
        } else if (!strcmp(a, "--log") && v) {
            o->log_dir = v;
            i++;
        } else if (!strcmp(a, "--no-log")) {
            o->log_dir = NULL;
        } else if (!strcmp(a, "--tmp") && v) {
            o->tmp_dir = v;
            i++;
        } else if (!strcmp(a, "--csr") && v) {
            if (parse_addr(v, &o->csr_phys)) {
                fprintf(stderr, "bad --csr address '%s'\n", v);
                return -1;
            }
            i++;
        } else if (!strcmp(a, "--buf") && v) {
            if (parse_addr(v, &o->buf_phys)) {
                fprintf(stderr, "bad --buf address '%s'\n", v);
                return -1;
            }
            i++;
        } else if (a[0] != '-' && !o->image) {
            o->image = a;
        } else {
            fprintf(stderr, "unknown or incomplete option '%s'\n", a);
            return -1;
        }
    }
    return 0;
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void log_test(const char *test, const char *mode, const char *config,
                     int pass, const char *details)
{
    const char *row[] = { reslog_timestamp(), reslog_host(), test, mode, config,
                          pass ? "PASS" : "FAIL", details };
    reslog_append(&RESLOG_TESTS, row);
}

/* "engines=1 lanes=8 width=64" from INFO */
static void hw_config(const fpga_t *f, char *buf, size_t len)
{
    snprintf(buf, len, "engines=%u lanes=%u width=%u", INFO_ENGINES(f->info),
             INFO_LANES(f->info), INFO_DATA_WIDTH(f->info));
}

static int make_tables(int scale, uint16_t qs[64], uint32_t r[64])
{
    if (qtable_scaled(scale, qs) != 0 || qtable_reciprocals(qs, r) != 0) {
        fprintf(stderr, "invalid Q scale %d\n", scale);
        return -1;
    }
    return 0;
}

/* Map the accelerator, confirm it is there, and leave it idle. */
static int open_accel(const opts_t *o, fpga_t *f)
{
    if (o->mode == MODE_DMA) {
        fprintf(stderr, "--mode dma is not built yet (phase 9: mSGDMA)\n");
        return -1;
    }
    if (fpga_open(f, o->csr_phys, o->buf_phys) != 0)
        return -1;
    uint32_t magic = fpga_rd(f, REG_MAGIC);
    if (magic != ACCEL_MAGIC) {
        fprintf(stderr, "no accelerator at 0x%08lx (MAGIC reads 0x%08x, expected 0x%08x); "
                "run 'dctq hwcheck' or check --csr\n", o->csr_phys, (unsigned)magic, ACCEL_MAGIC);
        fpga_close(f);
        return -1;
    }
    if (INFO_VERSION(f->info) != ACCEL_VERSION || INFO_DATA_WIDTH(f->info) != 64) {
        fprintf(stderr, "unsupported accelerator build (INFO 0x%08x)\n", (unsigned)f->info);
        fpga_close(f);
        return -1;
    }
    uint32_t st = fpga_soft_reset(f);
    if (st & (ST_BUSY | ST_ERROR)) {
        fprintf(stderr, "accelerator not idle after SOFT_RESET (STATUS 0x%08x)\n", (unsigned)st);
        fpga_close(f);
        return -1;
    }
    return 0;
}

/* ========================================================================= */
/* FPGA jobs: run nb blocks through mode reg or buf                          */
/* ========================================================================= */

typedef struct {
    double   transfer_s;   /* moving data in and out                    */
    double   compute_s;    /* START written until DONE seen             */
    uint64_t cycles;       /* sum of CYCLE_COUNT                        */
} job_times_t;

/* Option 1: one block per START. Transfer = everything except START..DONE. */
static int job_reg(fpga_t *f, const uint8_t *px, size_t nb, int16_t *q, job_times_t *t)
{
    memset(t, 0, sizeof *t);
    double t0 = timing_now();
    for (size_t b = 0; b < nb; b++) {
        double   cs;
        uint32_t cy;
        int rc = fpga_reg_block(f, px + 64 * b, q + 64 * b, &cs, &cy);
        t->compute_s += cs;
        t->cycles    += cy;
        if (rc != 0) {
            fprintf(stderr, "mode reg: block %zu failed (%s)\n", b, rc == -1 ? "timeout" : "ERROR");
            return rc;
        }
    }
    t->transfer_s = (timing_now() - t0) - t->compute_s;
    return 0;
}

/* Option 2: as many blocks per START as the buffer holds. */
static int job_buf(fpga_t *f, const uint8_t *px, size_t nb, int16_t *q, job_times_t *t)
{
    memset(t, 0, sizeof *t);
    size_t cap = f->buf_blocks;
    if (cap == 0 || cap * 64u * 3u > ACCEL_BUF_SPAN) {
        fprintf(stderr, "mode buf: BUF_BLOCKS = %zu does not fit the %u-byte window\n",
                cap, ACCEL_BUF_SPAN);
        return -2;
    }
    for (size_t done = 0; done < nb; ) {
        size_t   c = (nb - done < cap) ? nb - done : cap;
        uint32_t st;

        double t0 = timing_now();
        fpga_buf_write(f, BUF_IN_OFFSET, px + 64 * done, 64 * c);
        double t1 = timing_now();
        fpga_start(f, ACCEL_MODE_BUF, (uint32_t)c);
        int rc = fpga_wait(f, FPGA_TIMEOUT_S, &st);
        double t2 = timing_now();
        if (rc != 0) {
            fprintf(stderr, "mode buf: job of %zu blocks failed (%s, STATUS 0x%08x)\n", c,
                    rc == -1 ? "timeout" : "ERROR", (unsigned)st);
            return rc;
        }
        t->cycles += fpga_rd(f, REG_CYCLE_COUNT);
        fpga_buf_read(f, BUF_OUT_OFFSET(cap), q + 64 * done, 128 * c);
        double t3 = timing_now();

        t->transfer_s += (t1 - t0) + (t3 - t2);
        t->compute_s  += t2 - t1;
        done += c;
    }
    return 0;
}

static int job_fpga(int mode, fpga_t *f, const uint8_t *px, size_t nb, int16_t *q, job_times_t *t)
{
    return (mode == MODE_REG) ? job_reg(f, px, nb, q, t) : job_buf(f, px, nb, q, t);
}

/* Compare nb blocks; print the first few differences. Returns the number of bad blocks. */
static size_t compare_blocks(const int16_t *got, const int16_t *exp, size_t nb, int names)
{
    size_t bad = 0;
    for (size_t b = 0; b < nb; b++) {
        int first = -1;
        for (int i = 0; i < 64; i++)
            if (got[64 * b + i] != exp[64 * b + i]) {
                first = i;
                break;
            }
        if (first < 0)
            continue;
        if (bad < 10)
            printf("  MISMATCH block %zu%s%s%s, coefficient %d (%d,%d): got %d, expected %d\n",
                   b, names ? " (" : "", names ? test_block_name((int)b) : "", names ? ")" : "",
                   first, first / 8, first % 8, got[64 * b + first], exp[64 * b + first]);
        bad++;
    }
    return bad;
}

/* ========================================================================= */
/* selftest                                                                  */
/* ========================================================================= */

static int cmd_selftest(const opts_t *o)
{
    char summary[160];
    int failures = ref_selftest(o->tmp_dir, summary, sizeof summary);
    log_test("selftest (dctq)", "sw", "-", failures == 0, summary);
    return failures ? 1 : 0;
}

/* ========================================================================= */
/* hwcheck                                                                   */
/* ========================================================================= */

typedef struct { int pass, total; } tally_t;

static int check(tally_t *t, int ok, const char *name, const char *detail)
{
    printf("[%s] %-26s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
    t->total++;
    if (ok)
        t->pass++;
    return ok;
}

static int cmd_hwcheck(const opts_t *o)
{
    fpga_t  f;
    tally_t t = { 0, 0 };
    char    d[200], cfg[64] = "-", details[200];
    double  wr_mbs = 0, rd_mbs = 0;

    printf("hwcheck: registers at 0x%08lx, buffer at 0x%08lx\n", o->csr_phys, o->buf_phys);
    if (fpga_open(&f, o->csr_phys, o->buf_phys) != 0) {     /* PC stub explains itself */
        log_test("hwcheck", "hw", "-", 0, "cannot map the accelerator (not the board, or not root)");
        return 1;
    }

    /* 1. MAGIC: is the accelerator at this address at all? */
    uint32_t v = fpga_rd(&f, REG_MAGIC);
    snprintf(d, sizeof d, "0x%08x (expected 0x%08x)", (unsigned)v, ACCEL_MAGIC);
    if (!check(&t, v == ACCEL_MAGIC, "MAGIC", d)) {
        printf("\nThe accelerator is not at 0x%08lx. Check the FPGA is programmed, the bridges\n"
               "are enabled (LED test with devmem) and the address in Platform Designer.\n",
               o->csr_phys);
        log_test("hwcheck", "hw", "-", 0, "MAGIC wrong: accelerator not found");
        fpga_close(&f);
        return 1;
    }

    /* 2. INFO: which build is loaded */
    hw_config(&f, cfg, sizeof cfg);
    snprintf(d, sizeof d, "%s version=%u", cfg, INFO_VERSION(f.info));
    check(&t, INFO_VERSION(f.info) == ACCEL_VERSION && INFO_ENGINES(f.info) >= 1 &&
              INFO_LANES(f.info) >= 1 && INFO_DATA_WIDTH(f.info) == 64, "INFO", d);

    /* 3. SCRATCH: bus write/read in both directions */
    {
        static const uint32_t pat[] = { 0x00000000u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x5A5A5A5Au,
                                        0x12345678u, 0xDEADBEEFu };
        int bad = 0;
        for (size_t i = 0; i < sizeof pat / sizeof pat[0]; i++) {
            fpga_wr(&f, REG_SCRATCH, pat[i]);
            bad += fpga_rd(&f, REG_SCRATCH) != pat[i];
        }
        for (int b = 0; b < 32; b++) {                        /* walking one */
            fpga_wr(&f, REG_SCRATCH, 1u << b);
            bad += fpga_rd(&f, REG_SCRATCH) != (1u << b);
        }
        snprintf(d, sizeof d, "%d of 38 patterns wrong", bad);
        check(&t, bad == 0, "SCRATCH write/read", d);
    }

    /* 4. Q_TABLE: all 17 bits of all 64 entries, then the real table */
    uint16_t qs[64];
    uint32_t r[64];
    make_tables(1, qs, r);
    {
        int bad = 0;
        for (uint32_t i = 0; i < 64; i++)
            fpga_wr(&f, REG_Q_TABLE + 4 * i, (i * 2731u + 1u) & 0x1FFFFu);
        for (uint32_t i = 0; i < 64; i++)
            bad += fpga_rd(&f, REG_Q_TABLE + 4 * i) != ((i * 2731u + 1u) & 0x1FFFFu);
        fpga_load_qtable(&f, r);
        for (uint32_t i = 0; i < 64; i++)
            bad += fpga_rd(&f, REG_Q_TABLE + 4 * i) != r[i];
        snprintf(d, sizeof d, "%d of 128 reads wrong", bad);
        check(&t, bad == 0, "Q_TABLE write/read", d);
    }

    /* 5. SOFT_RESET leaves the accelerator idle */
    v = fpga_soft_reset(&f);
    snprintf(d, sizeof d, "STATUS 0x%08x", (unsigned)v);
    check(&t, (v & 0xFu) == 0, "SOFT_RESET -> idle", d);

    /* 6. ERROR on an invalid START (NUM_BLOCKS = 0 in mode 1), cleared by SOFT_RESET */
    fpga_start(&f, ACCEL_MODE_BUF, 0);
    v = fpga_rd(&f, REG_STATUS);
    uint32_t v2 = fpga_soft_reset(&f);
    snprintf(d, sizeof d, "STATUS 0x%08x, after SOFT_RESET 0x%08x", (unsigned)v, (unsigned)v2);
    check(&t, (v & 0x7u) == ST_ERROR && (v2 & 0xFu) == 0, "ERROR on bad START", d);

    /* 7. Buffer: pattern through the whole input region, with copy speed */
    size_t cap = f.buf_blocks;
    if (cap == 0 || cap * 64u * 3u > ACCEL_BUF_SPAN) {
        snprintf(d, sizeof d, "BUF_BLOCKS = %zu does not fit the window", cap);
        check(&t, 0, "buffer write/read", d);
        cap = 0;
    } else {
        size_t    bytes = cap * 64u, words = bytes / 4;
        uint32_t *src = malloc(bytes), *dst = malloc(bytes);
        if (!src || !dst) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        for (size_t i = 0; i < words; i++)
            src[i] = (uint32_t)i * 0x9E3779B9u ^ 0x5A5A5A5Au;
        double t0 = timing_now();
        fpga_buf_write(&f, BUF_IN_OFFSET, src, bytes);
        double t1 = timing_now();
        fpga_buf_read(&f, BUF_IN_OFFSET, dst, bytes);
        double t2 = timing_now();
        size_t bad = 0;
        for (size_t i = 0; i < words; i++)
            bad += src[i] != dst[i];
        wr_mbs = (double)bytes / (t1 - t0) / 1e6;
        rd_mbs = (double)bytes / (t2 - t1) / 1e6;
        snprintf(d, sizeof d, "%zu KB, %zu words wrong; write %.1f MB/s, read %.1f MB/s",
                 bytes / 1024, bad, wr_mbs, rd_mbs);
        check(&t, bad == 0, "buffer write/read", d);
        free(src);
        free(dst);
    }

    /* 8. One block through mode 0 (option 1), checked against the reference */
    {
        uint8_t  px[64];
        int16_t  got[64], exp[64];
        uint32_t cycles = 0;
        test_block(14, px);                                  /* JPEG example block */
        dct_ref_block(px, r, exp, NULL, NULL);
        int rc = fpga_reg_block(&f, px, got, NULL, &cycles);
        int same = rc == 0 && memcmp(got, exp, sizeof got) == 0;
        snprintf(d, sizeof d, "%s, %u cycles", rc ? "no DONE" : (same ? "matches reference" :
                 "DIFFERS from reference"), (unsigned)cycles);
        check(&t, same, "one block, mode reg", d);
    }

    /* 9. Four blocks through mode 1 (option 2), checked */
    if (cap >= 4) {
        uint8_t     px[4 * 64];
        int16_t     got[4 * 64], exp[4 * 64];
        job_times_t jt;
        for (int b = 0; b < 4; b++) {
            test_block(TEST_EDGE_BLOCKS + b, px + 64 * b);  /* first random blocks */
            dct_ref_block(px + 64 * b, r, exp + 64 * b, NULL, NULL);
        }
        int rc = job_buf(&f, px, 4, got, &jt);
        int same = rc == 0 && memcmp(got, exp, sizeof got) == 0;
        snprintf(d, sizeof d, "%s, %llu cycles", rc ? "no DONE" : (same ? "matches reference" :
                 "DIFFERS from reference"), (unsigned long long)jt.cycles);
        check(&t, same, "four blocks, mode buf", d);
    }

    fpga_soft_reset(&f);
    fpga_close(&f);

    int pass = t.pass == t.total;
    printf("\nHWCHECK %s: %d/%d checks passed (%s, buffer %zu blocks)\n",
           pass ? "PASSED" : "FAILED", t.pass, t.total, cfg, cap);
    snprintf(details, sizeof details, "%d/%d checks; buffer write %.1f MB/s, read %.1f MB/s",
             t.pass, t.total, wr_mbs, rd_mbs);
    log_test("hwcheck", "hw", cfg, pass, details);
    return pass ? 0 : 1;
}

/* ========================================================================= */
/* blocktest                                                                 */
/* ========================================================================= */

static int cmd_blocktest(const opts_t *o)
{
    fpga_t   f;
    uint16_t qs[64];
    uint32_t r[64];
    char     cfg[64] = "-", details[200], test[32];
    size_t   n = (size_t)test_block_count(o->nrand);

    snprintf(test, sizeof test, "blocktest x%d", o->scale);
    if (make_tables(o->scale, qs, r) != 0)
        return 2;

    uint8_t *px  = malloc(n * 64);
    int16_t *exp = malloc(n * 64 * sizeof *exp);
    int16_t *got = malloc(n * 64 * sizeof *got);
    if (!px || !exp || !got) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    for (size_t b = 0; b < n; b++) {
        test_block((int)b, px + 64 * b);
        dct_ref_block(px + 64 * b, r, exp + 64 * b, NULL, NULL);
    }

    printf("blocktest: %zu blocks (79 edge cases + %d random), mode %s, scale x%d\n",
           n, o->nrand, MODE_NAMES[o->mode], o->scale);

    int rc = 0;
    if (o->mode == MODE_SW) {
        dct_ref_blocks(px, n, r, got);           /* the batch routine used by 'run' */
    } else {
        if (open_accel(o, &f) != 0) {
            log_test(test, MODE_NAMES[o->mode], "-", 0, "accelerator not available");
            free(px); free(exp); free(got);
            return 1;
        }
        hw_config(&f, cfg, sizeof cfg);
        fpga_load_qtable(&f, r);
        job_times_t jt;
        rc = job_fpga(o->mode, &f, px, n, got, &jt);
        fpga_close(&f);
        if (rc == 0)
            printf("  FPGA: %llu cycles (%.1f per block)\n", (unsigned long long)jt.cycles,
                   (double)jt.cycles / (double)n);
    }

    size_t bad = (rc == 0) ? compare_blocks(got, exp, n, 1) : n;
    int pass = rc == 0 && bad == 0;
    if (rc != 0)
        snprintf(details, sizeof details, "%zu blocks: FPGA job failed", n);
    else
        snprintf(details, sizeof details, "%zu blocks, %zu bad (%zu coefficients checked)",
                 n, bad, n * 64);
    printf("BLOCKTEST %s: %s\n", pass ? "PASSED" : "FAILED", details);
    log_test(test, MODE_NAMES[o->mode], cfg, pass, details);

    free(px); free(exp); free(got);
    return pass ? 0 : 1;
}

/* ========================================================================= */
/* run                                                                       */
/* ========================================================================= */

static int cmd_run(const opts_t *o)
{
    rgb_image_t  rgb;
    gray_image_t gray, padded, rec_padded, rec;
    uint8_t     *blocks;
    size_t       n;
    uint16_t     qs[64];
    uint32_t     r[64];
    fpga_t       f;
    int          use_fpga = o->mode != MODE_SW;
    int          rc = 0;

    if (!o->image) {
        fprintf(stderr, "run: no image given\n");
        return 2;
    }
    if (make_tables(o->scale, qs, r) != 0)
        return 2;
    if (bmp_load_rgb(o->image, &rgb) != 0)
        return 1;
    if (preprocess_alloc(&rgb, &gray, &padded, &blocks, &n) != 0) {
        rgb_free(&rgb);
        return 1;
    }

    int      runs  = o->runs;
    int16_t *q_ref = malloc(n * 64 * sizeof *q_ref);
    int16_t *q_hw  = malloc(n * 64 * sizeof *q_hw);
    uint8_t *out   = malloc(n * 64);
    double  *sw_pre = malloc((size_t)runs * sizeof(double));
    double  *sw_cmp = malloc((size_t)runs * sizeof(double));
    double  *sw_tot = malloc((size_t)runs * sizeof(double));
    double  *hw_pre = malloc((size_t)runs * sizeof(double));
    double  *hw_xfr = malloc((size_t)runs * sizeof(double));
    double  *hw_cmp = malloc((size_t)runs * sizeof(double));
    double  *hw_tot = malloc((size_t)runs * sizeof(double));
    if (!q_ref || !q_hw || !out || !sw_pre || !sw_cmp || !sw_tot || !hw_pre || !hw_xfr ||
        !hw_cmp || !hw_tot || gray_alloc(&rec_padded, padded.width, padded.height) != 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    char size[32];
    snprintf(size, sizeof size, "%dx%d", gray.width, gray.height);

    /* ---- software baseline: always run, for speedup and the match check ---- */
    for (int i = -1; i < runs; i++) {                       /* i = -1: warm-up */
        double t0 = timing_now();
        preprocess_run(&rgb, &gray, &padded, blocks);
        double t1 = timing_now();
        dct_ref_blocks(blocks, n, r, q_ref);
        double t2 = timing_now();
        if (i >= 0) {
            sw_pre[i] = t1 - t0;
            sw_cmp[i] = t2 - t1;
            sw_tot[i] = t2 - t0;
        }
    }
    double sw_total = timing_median(sw_tot, runs);

    /* ---- FPGA path ---- */
    double   pre, xfr, cmp, tot;
    uint64_t cycles = 0;
    char     engines[16] = "-", lanes[16] = "-", cycles_s[32] = "-", match[48] = "-";
    const int16_t *q_use = q_ref;

    if (!use_fpga) {
        pre = timing_median(sw_pre, runs);
        xfr = 0.0;
        cmp = timing_median(sw_cmp, runs);
        tot = sw_total;
    } else {
        if (open_accel(o, &f) != 0) {
            rc = 1;
            goto done;
        }
        snprintf(engines, sizeof engines, "%u", INFO_ENGINES(f.info));
        snprintf(lanes, sizeof lanes, "%u", INFO_LANES(f.info));
        fpga_load_qtable(&f, r);

        for (int i = -1; i < runs; i++) {
            job_times_t jt;
            double t0 = timing_now();
            preprocess_run(&rgb, &gray, &padded, blocks);
            double t1 = timing_now();
            int jrc = job_fpga(o->mode, &f, blocks, n, q_hw, &jt);
            double t2 = timing_now();
            if (jrc != 0) {
                fpga_close(&f);
                fprintf(stderr, "run: FPGA job failed; no result recorded\n");
                rc = 1;
                goto done;
            }
            if (i >= 0) {
                hw_pre[i] = t1 - t0;
                hw_xfr[i] = jt.transfer_s;
                hw_cmp[i] = jt.compute_s;
                hw_tot[i] = t2 - t0;
            }
            cycles = jt.cycles;
        }
        fpga_close(&f);

        pre = timing_median(hw_pre, runs);
        xfr = timing_median(hw_xfr, runs);
        cmp = timing_median(hw_cmp, runs);
        tot = timing_median(hw_tot, runs);
        snprintf(cycles_s, sizeof cycles_s, "%llu", (unsigned long long)cycles);

        size_t bad = compare_blocks(q_hw, q_ref, n, 0);
        if (bad == 0)
            snprintf(match, sizeof match, "YES");
        else
            snprintf(match, sizeof match, "NO (%zu of %zu blocks)", bad, n);
        q_use = q_hw;
        if (bad)
            rc = 1;
    }

    /* ---- reconstruction and PSNR (from the coefficients this mode produced) ---- */
    reconstruct_blocks(q_use, n, qs, out);
    blocks_to_gray(out, &rec_padded);
    double mse  = gray_mse(&gray, &rec_padded, gray.width, gray.height);
    double psnr = psnr_from_mse(mse);

    if (o->recon) {
        if (gray_crop(&rec_padded, gray.width, gray.height, &rec) == 0) {
            if (bmp_save_gray(o->recon, &rec) != 0)
                rc = 1;
            gray_free(&rec);
        }
    }

    double speedup = sw_total / tot;

    /* ---- result ---- */
    printf("dctq run: %s %s (%zu blocks), mode %s, engines %s, scale x%d, median of %d runs\n",
           base_name(o->image), size, n, MODE_NAMES[o->mode], engines, o->scale, runs);
    printf("  preprocess  %10.3f ms\n", pre * 1e3);
    printf("  transfer    %10.3f ms\n", xfr * 1e3);
    printf("  compute     %10.3f ms\n", cmp * 1e3);
    printf("  total       %10.3f ms\n", tot * 1e3);
    printf("  sw total    %10.3f ms   speedup %.2fx\n", sw_total * 1e3, speedup);
    if (use_fpga)
        printf("  FPGA cycles %10s      (%.3f ms at 50 MHz)\n", cycles_s,
               (double)cycles / ACCEL_CLOCK_HZ * 1e3);
    printf("  match       %s\n", match);
    printf("  PSNR        %10.2f dB   (MSE %.3f)\n", psnr, mse);
    if (o->recon)
        printf("  recon       %s\n", o->recon);

    {
        char sc[8], rn[8], p[24], x[24], c[24], t[24], s[24], sp[16], ms[24], ps[24];
        snprintf(sc, sizeof sc, "%d", o->scale);
        snprintf(rn, sizeof rn, "%d", runs);
        snprintf(p, sizeof p, "%.3f", pre * 1e3);
        snprintf(x, sizeof x, "%.3f", xfr * 1e3);
        snprintf(c, sizeof c, "%.3f", cmp * 1e3);
        snprintf(t, sizeof t, "%.3f", tot * 1e3);
        snprintf(s, sizeof s, "%.3f", sw_total * 1e3);
        snprintf(sp, sizeof sp, "%.2f", speedup);
        snprintf(ms, sizeof ms, "%.3f", mse);
        if (isinf(psnr))
            snprintf(ps, sizeof ps, "inf");
        else
            snprintf(ps, sizeof ps, "%.2f", psnr);

        const char *run_row[] = { reslog_timestamp(), reslog_host(), base_name(o->image), size,
                                  MODE_NAMES[o->mode], engines, lanes, sc, rn, p, x, c, t, s,
                                  sp, cycles_s, match, ps };
        reslog_append(&RESLOG_RUNS, run_row);
        const char *q_row[] = { reslog_timestamp(), reslog_host(), base_name(o->image), size,
                                MODE_NAMES[o->mode], sc, ms, ps };
        reslog_append(&RESLOG_QUALITY, q_row);
    }

done:
    free(q_ref); free(q_hw); free(out);
    free(sw_pre); free(sw_cmp); free(sw_tot);
    free(hw_pre); free(hw_xfr); free(hw_cmp); free(hw_tot);
    free(blocks);
    gray_free(&gray);
    gray_free(&padded);
    gray_free(&rec_padded);
    rgb_free(&rgb);
    return rc;
}

/* ========================================================================= */

int main(int argc, char **argv)
{
    opts_t o;
    if (parse_args(argc, argv, &o) != 0) {
        usage();
        return 2;
    }
    reslog_set_dir(o.log_dir);

    int rc;
    if (!strcmp(o.cmd, "selftest"))
        rc = cmd_selftest(&o);
    else if (!strcmp(o.cmd, "hwcheck"))
        rc = cmd_hwcheck(&o);
    else if (!strcmp(o.cmd, "blocktest"))
        rc = cmd_blocktest(&o);
    else if (!strcmp(o.cmd, "run"))
        rc = cmd_run(&o);
    else {
        fprintf(stderr, "unknown command '%s'\n\n", o.cmd);
        usage();
        return 2;
    }

    if (o.log_dir && o.log_dir[0])
        printf("\nResults appended to %s/\n", o.log_dir);
    return rc;
}
