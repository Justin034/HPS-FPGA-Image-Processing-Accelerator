/*
 * timing.h - wall-clock timing for dctq.
 *
 * clock_gettime(CLOCK_MONOTONIC): unaffected by clock changes, nanosecond
 * resolution on both the PC and the board.
 */
#ifndef TIMING_H
#define TIMING_H

/* Current time in seconds (monotonic; only differences are meaningful). */
double timing_now(void);

/* Median of n values (n >= 1). The array is left unchanged. */
double timing_median(const double *v, int n);

#endif /* TIMING_H */
