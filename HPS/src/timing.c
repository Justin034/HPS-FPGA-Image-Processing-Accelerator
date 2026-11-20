/*
 * timing.c - wall-clock timing for dctq. See timing.h.
 */
#define _POSIX_C_SOURCE 200809L   /* clock_gettime */

#include "timing.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

double timing_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

double timing_median(const double *v, int n)
{
    if (n <= 0)
        return 0.0;
    double *s = malloc((size_t)n * sizeof *s);
    if (!s)
        return v[0];
    memcpy(s, v, (size_t)n * sizeof *s);
    qsort(s, (size_t)n, sizeof *s, cmp_double);
    double m = (n % 2) ? s[n / 2] : 0.5 * (s[n / 2 - 1] + s[n / 2]);
    free(s);
    return m;
}
