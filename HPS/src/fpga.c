/*
 * fpga.c - hardware layer: access to dct_accel through /dev/mem. See fpga.h.
 *
 * Two versions in one file, so one plain gcc command builds dctq anywhere:
 *   - real hardware layer when compiled for ARM (the board), or with -DDCTQ_HW
 *   - stubs otherwise (the PC), or with -DDCTQ_NO_HW
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64        /* off_t holds 0xFF210000 etc. without going negative */

#include "fpga.h"
#include "accel_regs.h"
#include "timing.h"

#include <stdio.h>
#include <string.h>

#if (defined(__arm__) || defined(DCTQ_HW)) && !defined(DCTQ_NO_HW)

/* ========================================================================= */
/* Real hardware layer (board)                                               */
/* ========================================================================= */
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int fpga_available(void) { return 1; }

/* Map [phys, phys + span) and return a pointer to phys. */
static volatile uint32_t *map_window(int fd, unsigned long phys, size_t span,
                                     void **map, size_t *len, const char *what)
{
    unsigned long page = (unsigned long)sysconf(_SC_PAGESIZE);
    unsigned long base = phys & ~(page - 1);
    unsigned long off  = phys - base;

    *len = (size_t)((off + span + page - 1) & ~(page - 1));
    *map = mmap(NULL, *len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)base);
    if (*map == MAP_FAILED) {
        fprintf(stderr, "fpga: cannot map %s at 0x%08lx: %s\n", what, phys, strerror(errno));
        *map = NULL;
        return NULL;
    }
    return (volatile uint32_t *)((volatile uint8_t *)*map + off);
}

int fpga_open(fpga_t *f, unsigned long csr_phys, unsigned long buf_phys)
{
    memset(f, 0, sizeof *f);
    f->fd       = -1;
    f->csr_phys = csr_phys;
    f->buf_phys = buf_phys;

    f->fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (f->fd < 0) {
        fprintf(stderr, "fpga: cannot open /dev/mem: %s (run as root)\n", strerror(errno));
        return -1;
    }
    f->csr = map_window(f->fd, csr_phys, ACCEL_CSR_SPAN, &f->csr_map, &f->csr_len, "registers");
    f->buf = map_window(f->fd, buf_phys, ACCEL_BUF_SPAN, &f->buf_map, &f->buf_len, "buffer");
    if (!f->csr || !f->buf) {
        fpga_close(f);
        return -1;
    }
    f->info       = fpga_rd(f, REG_INFO);
    f->buf_blocks = fpga_rd(f, REG_BUF_BLOCKS);
    return 0;
}

void fpga_close(fpga_t *f)
{
    if (f->csr_map)
        munmap(f->csr_map, f->csr_len);
    if (f->buf_map)
        munmap(f->buf_map, f->buf_len);
    if (f->fd >= 0)
        close(f->fd);
    f->csr_map = f->buf_map = NULL;
    f->csr = f->buf = NULL;
    f->fd = -1;
}

uint32_t fpga_rd(const fpga_t *f, uint32_t off)
{
    return f->csr[off / 4];
}

void fpga_wr(fpga_t *f, uint32_t off, uint32_t value)
{
    f->csr[off / 4] = value;
}

/* Explicit 32-bit accesses instead of memcpy: memcpy may use unaligned or
   multi-register accesses, which uncached device memory on ARM may not allow. */
void fpga_buf_write(fpga_t *f, uint32_t off, const void *src, size_t n)
{
    const uint8_t *s = src;
    volatile uint32_t *d = f->buf + off / 4;
    for (size_t i = 0; i < n / 4; i++) {
        uint32_t w;
        memcpy(&w, s + 4 * i, 4);
        d[i] = w;
    }
}

void fpga_buf_read(const fpga_t *f, uint32_t off, void *dst, size_t n)
{
    uint8_t *d = dst;
    const volatile uint32_t *s = f->buf + off / 4;
    for (size_t i = 0; i < n / 4; i++) {
        uint32_t w = s[i];
        memcpy(d + 4 * i, &w, 4);
    }
}

uint32_t fpga_soft_reset(fpga_t *f)
{
    fpga_wr(f, REG_CTRL, CTRL_SOFT_RESET);
    return fpga_rd(f, REG_STATUS);
}

void fpga_load_qtable(fpga_t *f, const uint32_t recip[64])
{
    for (int i = 0; i < 64; i++)
        fpga_wr(f, REG_Q_TABLE + 4u * (uint32_t)i, recip[i]);
}

void fpga_start(fpga_t *f, int mode, uint32_t nblocks)
{
    if (mode != ACCEL_MODE_REG)
        fpga_wr(f, REG_NUM_BLOCKS, nblocks);
    fpga_wr(f, REG_CTRL, CTRL_START | CTRL_MODE(mode));
}

int fpga_wait(fpga_t *f, double timeout_s, uint32_t *status)
{
    double   t0 = timing_now();
    uint32_t st;

    for (;;) {
        st = fpga_rd(f, REG_STATUS);
        if (!(st & ST_BUSY))
            break;
        if (timing_now() - t0 > timeout_s) {
            fprintf(stderr, "fpga: timeout after %.3f s (STATUS 0x%08x); SOFT_RESET issued\n",
                    timeout_s, (unsigned)st);
            fpga_soft_reset(f);
            if (status)
                *status = st;
            return -1;
        }
    }
    if (status)
        *status = st;
    if ((st & ST_ERROR) || !(st & ST_DONE))
        return -2;
    return 0;
}

int fpga_reg_block(fpga_t *f, const uint8_t px[64], int16_t q[64],
                   double *compute_s, uint32_t *cycles)
{
    for (int i = 0; i < 64; i++)
        fpga_wr(f, REG_IN_BLOCK + 4u * (uint32_t)i, px[i]);

    double t0 = timing_now();
    fpga_start(f, ACCEL_MODE_REG, 1);
    int rc = fpga_wait(f, FPGA_TIMEOUT_S, NULL);
    double t1 = timing_now();
    if (compute_s)
        *compute_s = t1 - t0;
    if (cycles)
        *cycles = fpga_rd(f, REG_CYCLE_COUNT);
    if (rc != 0)
        return rc;

    for (int i = 0; i < 64; i++)
        q[i] = (int16_t)fpga_rd(f, REG_OUT_BLOCK + 4u * (uint32_t)i);
    return 0;
}

#else

/* ========================================================================= */
/* Stubs (PC build): no FPGA here                                            */
/* ========================================================================= */

int fpga_available(void) { return 0; }

int fpga_open(fpga_t *f, unsigned long csr_phys, unsigned long buf_phys)
{
    memset(f, 0, sizeof *f);
    f->fd       = -1;
    f->csr_phys = csr_phys;
    f->buf_phys = buf_phys;
    fprintf(stderr, "fpga: hardware not available in this build (PC); only --mode sw works here\n");
    return -1;
}

void     fpga_close(fpga_t *f)                                 { (void)f; }
uint32_t fpga_rd(const fpga_t *f, uint32_t off)                { (void)f; (void)off; return 0; }
void     fpga_wr(fpga_t *f, uint32_t off, uint32_t value)      { (void)f; (void)off; (void)value; }
void     fpga_buf_write(fpga_t *f, uint32_t off, const void *src, size_t n)
                                                               { (void)f; (void)off; (void)src; (void)n; }
void     fpga_buf_read(const fpga_t *f, uint32_t off, void *dst, size_t n)
                                                               { (void)f; (void)off; (void)dst; (void)n; }
uint32_t fpga_soft_reset(fpga_t *f)                            { (void)f; return 0; }
void     fpga_load_qtable(fpga_t *f, const uint32_t recip[64]) { (void)f; (void)recip; }
void     fpga_start(fpga_t *f, int mode, uint32_t nblocks)     { (void)f; (void)mode; (void)nblocks; }

int fpga_wait(fpga_t *f, double timeout_s, uint32_t *status)
{
    (void)f; (void)timeout_s;
    if (status)
        *status = 0;
    return -2;
}

int fpga_reg_block(fpga_t *f, const uint8_t px[64], int16_t q[64],
                   double *compute_s, uint32_t *cycles)
{
    (void)f; (void)px; (void)q; (void)compute_s; (void)cycles;
    return -2;
}

#endif
