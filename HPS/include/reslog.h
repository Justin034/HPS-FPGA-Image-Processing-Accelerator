/*
 * reslog.h - automatic result recording.
 *
 * Every test or measurement appends one row to a table, written twice:
 *   <dir>/<table>.md   markdown table (renders on GitHub, readable as text)
 *   <dir>/<table>.csv  same data for a spreadsheet
 * The header is written automatically when a file is new or empty.
 * The directory is created if needed.
 *
 * Default directories: "results" on the board (dctq), "docs/logs" on the PC (make c).
 */
#ifndef RESLOG_H
#define RESLOG_H

#ifdef __cplusplus
extern "C" {      /* also compiled into the Verilator (C++) testbench */
#endif

typedef struct {
    const char *name;            /* file name without extension, e.g. "tests" */
    const char *title;           /* heading written at the top of the .md file */
    int ncols;
    const char *const *cols;
} reslog_table_t;

/* Standard tables shared by all programs (columns listed in reslog.c). */
extern const reslog_table_t RESLOG_TESTS;     /* Date, Host, Test, Mode, Config, Result, Details */
extern const reslog_table_t RESLOG_RUNS;      /* timing, speedup, match, PSNR per run */
extern const reslog_table_t RESLOG_QUALITY;   /* PSNR per image and Q scale */
extern const reslog_table_t RESLOG_SIM;       /* Verilator results: config, seed, cycles/block */

/* Set the output directory; NULL or "" disables logging. Default: disabled. */
void reslog_set_dir(const char *dir);
const char *reslog_dir(void);

/* Append one row; values[i] matches table->cols[i]. NULL values are written as "-".
   Returns 0 on success, -1 on error (a warning is printed; programs carry on). */
int reslog_append(const reslog_table_t *table, const char *const *values);

/* "YYYY-MM-DD HH:MM:SS", local time (static buffer). */
const char *reslog_timestamp(void);

/* Host name of this machine (static buffer), so PC and board rows can be told apart. */
const char *reslog_host(void);

#ifdef __cplusplus
}
#endif

#endif /* RESLOG_H */
