/*
 * test_blocks.h - deterministic 8x8 test blocks.
 *
 * Used by the PC self-test, the board's blocktest and the Verilator
 * testbench (through DPI-C), so every place tests the same data.
 *
 *   index 0 .. 14    named edge cases (all 0, all 255, checkerboards, ramps, ...)
 *   index 15 .. 78   64 "worst-case" blocks, one per coefficient (overflow checks)
 *   index 79 ..      random blocks; each derives from its own index, so any
 *                    block can be generated on its own, in any order
 */
#ifndef TEST_BLOCKS_H
#define TEST_BLOCKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {      /* also compiled into the Verilator (C++) testbench */
#endif

#define TEST_NAMED_BLOCKS   15
#define TEST_EDGE_BLOCKS    79      /* named + worst-case */
#define TEST_DEFAULT_RANDOM 200

/* Classic JPEG example block, row-major. */
extern const uint8_t TEST_SAMPLE_BLOCK[64];

/* Total blocks for a run with n_random random blocks. */
int test_block_count(int n_random);

/* Fill px with block number `index` (row-major). index must be >= 0. */
void test_block(int index, uint8_t px[64]);

/* Short description of block `index` (points to a static buffer for some blocks). */
const char *test_block_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* TEST_BLOCKS_H */
