/*
 * ref_selftest.h - checks the C reference against floating point and itself.
 * Run on the PC (make c) and on the board (dctq selftest).
 */
#ifndef REF_SELFTEST_H
#define REF_SELFTEST_H

#include <stddef.h>

/*
 * Runs all checks, printing one [PASS]/[FAIL] line per check.
 * tmp_dir: writable directory for the BMP round-trip test ("." if NULL).
 * summary: receives e.g. "13/13 checks passed; max |D - float| 1.62" (may be NULL).
 * Returns the number of failed checks (0 = all passed).
 */
int ref_selftest(const char *tmp_dir, char *summary, size_t summary_len);

#endif /* REF_SELFTEST_H */
