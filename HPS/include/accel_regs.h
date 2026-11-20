/*
 * accel_regs.h - dct_accel register map and default addresses, as seen by the HPS.
 *
 * Mirrors FPGA/rtl/dct_accel.sv and FPGA/DESIGN.md §10. If the RTL register map
 * changes, this file changes with it.
 */
#ifndef ACCEL_REGS_H
#define ACCEL_REGS_H

/* ---- Physical addresses (override with dctq --csr / --buf) ----
 * Assign exactly these in Platform Designer (phase 6):
 *   CSR slave    : lightweight HPS-to-FPGA bridge, offset 0x10000
 *   buffer slave : main HPS-to-FPGA bridge, offset 0x04000000                    */
#define ACCEL_CSR_PHYS      0xFF210000u
#define ACCEL_CSR_SPAN      0x400u          /* 1 KB of registers               */
#define ACCEL_BUF_PHYS      0xC4000000u
#define ACCEL_BUF_SPAN      0x40000u        /* 256 KB address window           */

#define ACCEL_CLOCK_HZ      50000000.0      /* FPGA clock: CYCLE_COUNT -> seconds */

/* ---- Register byte offsets ---- */
#define REG_CTRL            0x000u
#define REG_STATUS          0x004u
#define REG_NUM_BLOCKS      0x008u
#define REG_CYCLE_COUNT     0x00Cu
#define REG_INFO            0x010u
#define REG_BUF_BLOCKS      0x014u
#define REG_MAGIC           0x018u
#define REG_SCRATCH         0x01Cu
#define REG_Q_TABLE         0x100u          /* 64 words: reciprocals R (17 bit)   */
#define REG_IN_BLOCK        0x200u          /* 64 words: pixels (mode 0)          */
#define REG_OUT_BLOCK       0x300u          /* 64 words: coefficients, sign-extended */

/* ---- CTRL ---- */
#define CTRL_START          0x1u
#define CTRL_SOFT_RESET     0x2u
#define CTRL_MODE(m)        ((unsigned)(m) << 4)

#define ACCEL_MODE_REG      0               /* option 1: IN_BLOCK / OUT_BLOCK     */
#define ACCEL_MODE_BUF      1               /* option 2: internal buffer          */
#define ACCEL_MODE_STREAM   2               /* options 3/4: mSGDMA streams        */

/* ---- STATUS ---- */
#define ST_BUSY             0x1u
#define ST_DONE             0x2u
#define ST_ERROR            0x4u
#define ST_ENGINES_BUSY     0x8u

/* ---- INFO ---- */
#define INFO_ENGINES(v)     ((unsigned)(v) & 0xFFu)
#define INFO_LANES(v)       (((unsigned)(v) >> 8) & 0xFFu)
#define INFO_DATA_WIDTH(v)  (((unsigned)(v) >> 16) & 0xFFFu)
#define INFO_VERSION(v)     (((unsigned)(v) >> 28) & 0xFu)

#define ACCEL_MAGIC         0x44435431u     /* "DCT1" */
#define ACCEL_VERSION       1u

/* ---- Buffer layout (mode 1) ----
 * Input pixels start at byte 0. Output coefficients start right after the
 * input region; with the RTL rule BUF_OUT_WORDS = 2 * BUF_IN_WORDS that is
 * BUF_BLOCKS * 64 bytes (0x10000 for the default 1024 blocks).               */
#define BUF_IN_OFFSET       0u
#define BUF_OUT_OFFSET(buf_blocks) ((unsigned)(buf_blocks) * 64u)

#endif /* ACCEL_REGS_H */
