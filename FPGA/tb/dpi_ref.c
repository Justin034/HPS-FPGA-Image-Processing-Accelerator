/*
 * dpi_ref.c - DPI-C glue between the Verilator testbenches and the C reference.
 *
 * The testbench asks the C reference (HPS/src/dct_ref.c, test_blocks.c) for test
 * pixels and expected results instead of reading vector files, and logs its
 * result through reslog.c. All values are passed as int / strings.
 */
#include "dct_ref.h"
#include "reslog.h"
#include "test_blocks.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cache: the testbench asks for one value at a time, so compute a block once. */
static int     px_block = -1;
static uint8_t px_cache[DCT_BLOCK];

static int     ref_block = -1, ref_scale = -1;
static int16_t ref_q[DCT_BLOCK];
static int32_t ref_t[DCT_BLOCK], ref_d[DCT_BLOCK];

static void load_pixels(int block)
{
    if (block != px_block) {
        test_block(block, px_cache);
        px_block = block;
    }
}

static void compute(int block, int scale)
{
    if (block == ref_block && scale == ref_scale)
        return;
    uint16_t qs[DCT_BLOCK];
    uint32_t r[DCT_BLOCK];
    if (qtable_scaled(scale, qs) != 0 || qtable_reciprocals(qs, r) != 0) {
        fprintf(stderr, "dpi_ref: invalid scale %d\n", scale);
        return;
    }
    load_pixels(block);
    dct_ref_block(px_cache, r, ref_q, ref_t, ref_d);
    ref_block = block;
    ref_scale = scale;
}

int dpi_test_block_count(int n_random)        { return test_block_count(n_random); }
const char *dpi_test_block_name(int block)    { return test_block_name(block); }

int dpi_test_pixel(int block, int i)
{
    load_pixels(block);
    return px_cache[i & 63];
}

int dpi_recip(int scale, int i)
{
    uint16_t qs[DCT_BLOCK];
    uint32_t r[DCT_BLOCK];
    if (qtable_scaled(scale, qs) != 0 || qtable_reciprocals(qs, r) != 0)
        return 0;
    return (int)r[i & 63];
}

int dpi_ref_q(int block, int scale, int i) { compute(block, scale); return ref_q[i & 63]; }
int dpi_ref_t(int block, int scale, int i) { compute(block, scale); return ref_t[i & 63]; }
int dpi_ref_d(int block, int scale, int i) { compute(block, scale); return ref_d[i & 63]; }

void dpi_log_sim(const char *logdir, const char *test, const char *cfg, int seed,
                 int blocks, const char *cycles_per_block, const char *result,
                 const char *details)
{
    char seed_s[16], blocks_s[16];
    snprintf(seed_s, sizeof seed_s, "%d", seed);
    snprintf(blocks_s, sizeof blocks_s, "%d", blocks);
    reslog_set_dir(logdir);
    const char *row[] = { reslog_timestamp(), reslog_host(), test, cfg, seed_s,
                          blocks_s, cycles_per_block, result, details };
    reslog_append(&RESLOG_SIM, row);
}

#ifdef __cplusplus
}
#endif
