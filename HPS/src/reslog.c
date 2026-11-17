/*
 * reslog.c - automatic result recording. See reslog.h.
 */
#define _XOPEN_SOURCE 700   /* mkdir, localtime_r, gethostname */

#include "reslog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *const TESTS_COLS[] = {
    "Date", "Host", "Test", "Mode", "Config", "Result", "Details",
};
static const char *const RUNS_COLS[] = {
    "Date", "Host", "Image", "Size", "Mode", "Engines", "Lanes", "Scale", "Runs",
    "Preprocess ms", "Transfer ms", "Compute ms", "Total ms", "SW total ms",
    "Speedup", "Cycles", "Match", "PSNR dB",
};
static const char *const QUALITY_COLS[] = {
    "Date", "Host", "Image", "Size", "Mode", "Scale", "MSE", "PSNR dB",
};

static const char *const SIM_COLS[] = {
    "Date", "Host", "Test", "Config", "Seed", "Blocks", "Cycles/block", "Result", "Details",
};

#define NCOLS(a) ((int)(sizeof(a) / sizeof((a)[0])))

const reslog_table_t RESLOG_SIM     = { "sim",     "Simulation results (Verilator)", NCOLS(SIM_COLS),    SIM_COLS };

const reslog_table_t RESLOG_TESTS   = { "tests",   "Test results",                  NCOLS(TESTS_COLS),   TESTS_COLS };
const reslog_table_t RESLOG_RUNS    = { "runs",    "Timing runs",                   NCOLS(RUNS_COLS),    RUNS_COLS };
const reslog_table_t RESLOG_QUALITY = { "quality", "Reconstruction quality (PSNR)", NCOLS(QUALITY_COLS), QUALITY_COLS };

static char g_dir[512] = "";

void reslog_set_dir(const char *dir)
{
    if (!dir) {
        g_dir[0] = '\0';
        return;
    }
    snprintf(g_dir, sizeof g_dir, "%s", dir);
}

const char *reslog_dir(void)
{
    return g_dir;
}

/* mkdir -p */
static int make_dirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0775) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(buf, 0775) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

/* Open for append; *was_empty tells the caller to write a header first. */
static FILE *open_append(const char *path, int *was_empty)
{
    FILE *f = fopen(path, "a");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    *was_empty = (ftell(f) == 0);
    return f;
}

static void md_cell(FILE *f, const char *s)
{
    fputs(" ", f);
    for (; *s; s++) {
        if (*s == '|')
            fputs("\\|", f);
        else if (*s == '\n' || *s == '\r')
            fputc(' ', f);
        else
            fputc(*s, f);
    }
    fputs(" |", f);
}

static void csv_cell(FILE *f, const char *s, int first)
{
    if (!first)
        fputc(',', f);
    if (strpbrk(s, ",\"\n\r") == NULL) {
        fputs(s, f);
        return;
    }
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"')
            fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

int reslog_append(const reslog_table_t *t, const char *const *values)
{
    if (g_dir[0] == '\0')
        return 0;                                  /* logging disabled */

    if (make_dirs(g_dir) != 0) {
        fprintf(stderr, "warning: cannot create log directory %s\n", g_dir);
        return -1;
    }

    char path[600];
    int empty = 0;
    int rc = 0;

    /* markdown */
    snprintf(path, sizeof path, "%s/%s.md", g_dir, t->name);
    FILE *f = open_append(path, &empty);
    if (!f) {
        fprintf(stderr, "warning: cannot write %s\n", path);
        return -1;
    }
    if (empty) {
        fprintf(f, "# %s\n\n|", t->title);
        for (int i = 0; i < t->ncols; i++)
            md_cell(f, t->cols[i]);
        fputs("\n|", f);
        for (int i = 0; i < t->ncols; i++)
            fputs("---|", f);
        fputc('\n', f);
    }
    fputc('|', f);
    for (int i = 0; i < t->ncols; i++)
        md_cell(f, values[i] ? values[i] : "-");
    fputc('\n', f);
    if (fclose(f) != 0)
        rc = -1;

    /* csv */
    snprintf(path, sizeof path, "%s/%s.csv", g_dir, t->name);
    f = open_append(path, &empty);
    if (!f) {
        fprintf(stderr, "warning: cannot write %s\n", path);
        return -1;
    }
    if (empty) {
        for (int i = 0; i < t->ncols; i++)
            csv_cell(f, t->cols[i], i == 0);
        fputc('\n', f);
    }
    for (int i = 0; i < t->ncols; i++)
        csv_cell(f, values[i] ? values[i] : "-", i == 0);
    fputc('\n', f);
    if (fclose(f) != 0)
        rc = -1;

    if (rc != 0)
        fprintf(stderr, "warning: error while writing logs in %s\n", g_dir);
    return rc;
}

const char *reslog_timestamp(void)
{
    static char buf[32];
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL || strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm) == 0)
        snprintf(buf, sizeof buf, "unknown");
    return buf;
}

const char *reslog_host(void)
{
    static char buf[64];
    if (gethostname(buf, sizeof buf) != 0)
        snprintf(buf, sizeof buf, "unknown");
    buf[sizeof buf - 1] = '\0';
    return buf;
}
