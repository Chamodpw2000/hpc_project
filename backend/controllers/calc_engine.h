/*
 * Score Calculation Engine
 * Serial and OpenMP-parallel statistical computation on score arrays.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CALC_ENGINE_H
#define CALC_ENGINE_H

#include <stddef.h>

/* Result struct returned by both run_serial() and run_parallel() */
typedef struct {
    double sum;
    double mean;
    double variance;
    double stddev;
    double min;
    double max;
    int    grade_A;          /* score >= 90 */
    int    grade_B;          /* score >= 80 */
    int    grade_C;          /* score >= 70 */
    int    grade_D;          /* score >= 60 */
    int    grade_F;          /* score <  60 */
    double sort_time_ms;     /* time spent sorting (for median) */
    double median;
    int    count;
    double elapsed_ms;       /* total wall-clock time */
    int    threads_used;
} calc_result_t;

/* Run fully serial (single-thread) calculation */
calc_result_t run_serial(const double *scores, int n);

/* Run OpenMP-parallelised calculation */
calc_result_t run_parallel(const double *scores, int n);

/*
 * Serialise a calc_result_t into a JSON fragment stored in buf[0..sz-1].
 * label – e.g. "serial" or "parallel"
 */
void format_result_json(char *buf, size_t sz,
                        const calc_result_t *r, const char *label);

#endif /* CALC_ENGINE_H */
