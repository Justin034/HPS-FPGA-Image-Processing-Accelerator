/*
 * test_blocks.c - deterministic 8x8 test blocks. See test_blocks.h.
 *
 * Only integer arithmetic is used, so the blocks are identical on the PC,
 * on the ARM board and inside the Verilator simulation.
 */
#include "test_blocks.h"
#include "dct_ref.h"

#include <stdio.h>

const uint8_t TEST_SAMPLE_BLOCK[64] = {
    52, 55, 61,  66,  70,  61, 64, 73,
    63, 59, 55,  90, 109,  85, 69, 72,
    62, 59, 68, 113, 144, 104, 66, 73,
    63, 58, 71, 122, 154, 106, 70, 69,
    67, 61, 68, 104, 126,  88, 68, 70,
    79, 65, 60,  70,  77,  68, 58, 75,
    85, 71, 64,  59,  55,  61, 65, 83,
    87, 79, 69,  68,  65,  76, 78, 94,
};

static const char *const NAMED[TEST_NAMED_BLOCKS] = {
    "all 0",
    "all 255",
    "all 128 (zero after level shift)",
    "all 127",
    "checkerboard 0/255",
    "checkerboard 255/0",
    "horizontal ramp",
    "vertical ramp",
    "diagonal ramp",
    "vertical stripes 0/255",
    "horizontal stripes 0/255",
    "impulse 255 at (0,0)",
    "impulse 0 at (7,7)",
    "count 0..63",
    "JPEG example block",
};

int test_block_count(int n_random)
{
    return TEST_EDGE_BLOCKS + (n_random > 0 ? n_random : 0);
}

/* xorshift32: small, fast, identical everywhere */
static uint32_t next_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void named_block(int index, uint8_t px[64])
{
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int v;
            switch (index) {
            case 0:  v = 0; break;
            case 1:  v = 255; break;
            case 2:  v = 128; break;
            case 3:  v = 127; break;
            case 4:  v = ((x + y) & 1) ? 255 : 0; break;
            case 5:  v = ((x + y) & 1) ? 0 : 255; break;
            case 6:  v = 64 + x * 16; break;
            case 7:  v = 64 + y * 16; break;
            case 8:  v = (x + y) * 18; break;
            case 9:  v = (x & 1) ? 255 : 0; break;
            case 10: v = (y & 1) ? 255 : 0; break;
            case 11: v = (x == 0 && y == 0) ? 255 : 128; break;
            case 12: v = (x == 7 && y == 7) ? 0 : 128; break;
            case 13: v = y * 8 + x; break;
            default: v = TEST_SAMPLE_BLOCK[y * 8 + x]; break;   /* 14 */
            }
            px[y * 8 + x] = (uint8_t)v;
        }
    }
}

/* Block that maximises |D[v][u]|: 255 where C[v][y]*C[u][x] > 0, else 0.
   (No entry of the DCT matrix is zero, so the sign is always defined.) */
static void worst_case_block(int k, uint8_t px[64])
{
    int v = k / 8, u = k % 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int same_sign = (DCT_Q15[v * 8 + y] > 0) == (DCT_Q15[u * 8 + x] > 0);
            px[y * 8 + x] = same_sign ? 255 : 0;
        }
}

static void random_block(int r, uint8_t px[64])
{
    uint32_t s = ((uint32_t)r * 2654435761u) ^ 0x9E3779B9u;
    if (s == 0)
        s = 1;
    for (int i = 0; i < 4; i++)          /* mix the seed */
        next_rand(&s);

    if ((r & 1) == 0) {                  /* uniform noise */
        for (int i = 0; i < 64; i++)
            px[i] = (uint8_t)(next_rand(&s) & 0xFF);
        return;
    }
    /* smooth: base level + gradient + small noise (closer to real images) */
    int base = (int)(next_rand(&s) % 256);
    int gx = (int)(next_rand(&s) % 41) - 20;
    int gy = (int)(next_rand(&s) % 41) - 20;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int noise = (int)(next_rand(&s) % 13) - 6;
            int v = base + (gx * (2 * x - 7)) / 2 + (gy * (2 * y - 7)) / 2 + noise;
            px[y * 8 + x] = clamp_u8(v);
        }
}

void test_block(int index, uint8_t px[64])
{
    if (index < TEST_NAMED_BLOCKS)
        named_block(index < 0 ? 0 : index, px);
    else if (index < TEST_EDGE_BLOCKS)
        worst_case_block(index - TEST_NAMED_BLOCKS, px);
    else
        random_block(index - TEST_EDGE_BLOCKS, px);
}

const char *test_block_name(int index)
{
    static char buf[64];
    if (index >= 0 && index < TEST_NAMED_BLOCKS)
        return NAMED[index];
    if (index >= TEST_NAMED_BLOCKS && index < TEST_EDGE_BLOCKS) {
        int k = index - TEST_NAMED_BLOCKS;
        snprintf(buf, sizeof buf, "worst case for coefficient (%d,%d)", k / 8, k % 8);
        return buf;
    }
    return ((index - TEST_EDGE_BLOCKS) & 1) ? "random smooth" : "random uniform";
}
