/*
 * Correlation Calculation Engine
 * Serial and OpenMP-parallel Pearson correlation computation.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CORRELATION_ENGINE_H
#define CORRELATION_ENGINE_H

#include "../include/db.h"  /* score_pair_t */
#include <stddef.h>

/* Result of a correlation run */
typedef struct {
    double correlation_coefficient;  /* Pearson r, in [-1, 1] */
    double elapsed_ms;               /* wall-clock computation time */
    int    threads_used;
    int    n_pairs;                  /* number of matched data points */
    double best_fit_slope;           /* regression line: y = slope*x + intercept */
    double best_fit_intercept;
} corr_result_t;

/* Single-threaded Pearson r over n pairs */
corr_result_t run_corr_serial  (const score_pair_t *pairs, int n);

/* OpenMP-parallelised Pearson r (five independent reductions) */
corr_result_t run_corr_parallel(const score_pair_t *pairs, int n);

/* POSIX-threads (pthreads) parallelised Pearson r */
corr_result_t run_corr_pthread(const score_pair_t *pairs, int n);

/*
 * Build a JSON fragment for a correlation result.
 * Returns a malloc'd string; caller must free().
 * label        – "serial" or "parallel"
 * db_fetch_ms  – time spent fetching pairs from MongoDB
 * points/npts  – scatter data included in the response
 */
char* format_corr_json(const corr_result_t *r, const char *label,
                       double db_fetch_ms,
                       const score_pair_t *points, int npts,
                       int total_students, int excluded);

/* Compact JSON fragment (no data_points) for the all-subjects endpoint. Caller must free(). */
char* format_corr_line_json(const corr_result_t *r, const char *label);

#endif /* CORRELATION_ENGINE_H */
