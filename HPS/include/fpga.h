/*
 * fpga.h - hardware layer: access to dct_accel from Linux on the HPS.
 *
 * The accelerator's two Avalon-MM slaves are mapped into the program through
 * /dev/mem (needs root):
 *   registers : lightweight bridge  (ACCEL_CSR_PHYS by default)
 *   buffer    : main bridge         (ACCEL_BUF_PHYS by default, mode 1 only)
 * /dev/mem is opened with O_SYNC, so the mappings are uncached: every read and
 * write goes to the FPGA.
 *
 * Built in two versions from fpga.c:
 *   ARM (the board)  : the real hardware layer
 *   anything else    : stubs; fpga_available() returns 0 and fpga_open() fails,
 *                      so dctq builds and runs in software mode on the PC.
 * Force the real version with -DDCTQ_HW, or the stubs with -DDCTQ_NO_HW.
 */
#ifndef FPGA_H
#define FPGA_H

#include <stddef.h>
#include <stdint.h>

#define FPGA_TIMEOUT_S 1.0     /* default wait for DONE */

typedef struct {
    unsigned long      csr_phys, buf_phys;
    int                fd;
    volatile uint32_t *csr;        /* register window                 */
    volatile uint32_t *buf;        /* buffer window (mode 1)          */
    void              *csr_map, *buf_map;
    size_t             csr_len, buf_len;
    uint32_t           info;       /* INFO register, read at open     */
    uint32_t           buf_blocks; /* BUF_BLOCKS register, read at open */
} fpga_t;

/* 1 if this build contains the real hardware layer. */
int fpga_available(void);

/* Map both windows. Does not touch the accelerator except to read INFO and
   BUF_BLOCKS. Returns 0, or -1 with a message on stderr. */
int  fpga_open(fpga_t *f, unsigned long csr_phys, unsigned long buf_phys);
void fpga_close(fpga_t *f);

/* Register access; off is a byte offset from accel_regs.h. */
uint32_t fpga_rd(const fpga_t *f, uint32_t off);
void     fpga_wr(fpga_t *f, uint32_t off, uint32_t value);

/* CPU copies to/from the buffer window with 32-bit accesses.
   off and n must be multiples of 4. */
void fpga_buf_write(fpga_t *f, uint32_t off, const void *src, size_t n);
void fpga_buf_read(const fpga_t *f, uint32_t off, void *dst, size_t n);

/* SOFT_RESET; returns STATUS read straight afterwards (0 when it worked). */
uint32_t fpga_soft_reset(fpga_t *f);

/* Write the 64 reciprocals to Q_TABLE. */
void fpga_load_qtable(fpga_t *f, const uint32_t recip[64]);

/* Write NUM_BLOCKS (modes 1, 2) and CTRL = START | MODE. */
void fpga_start(fpga_t *f, int mode, uint32_t nblocks);

/*
 * Poll STATUS until BUSY clears or timeout_s passes.
 * Returns  0  DONE without ERROR
 *         -1  timeout (SOFT_RESET has been issued)
 *         -2  finished with ERROR, or not DONE
 * *status receives the last STATUS value (may be NULL).
 */
int fpga_wait(fpga_t *f, double timeout_s, uint32_t *status);

/*
 * One block in mode 0 (option 1): write 64 pixels to IN_BLOCK, START, wait,
 * read 64 coefficients from OUT_BLOCK.
 * compute_s receives the time from START to DONE seen, cycles the
 * CYCLE_COUNT register (either may be NULL). Returns as fpga_wait.
 */
int fpga_reg_block(fpga_t *f, const uint8_t px[64], int16_t q[64],
                   double *compute_s, uint32_t *cycles);

#endif /* FPGA_H */
