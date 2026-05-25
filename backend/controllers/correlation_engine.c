/*
 * Correlation Calculation Engine
 * Serial and OpenMP-parallel Pearson r using the single-pass computational formula:
 *
 *        n·Σ(xᵢyᵢ) − Σxᵢ · Σyᵢ
 *   r = ──────────────────────────────────────────────────
 *       √[(n·Σxᵢ² − (Σxᵢ)²) · (n·Σyᵢ² − (Σyᵢ)²)]
 *
 * Five running sums are accumulated in a single O(n) pass.
 * Copyright (c) 2026
 * MIT License
 */

#include "correlation_engine.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <stdio.h>

corr_result_t run_corr_serial(const score_pair_t *pairs, int n)
{
    corr_result_t r;
    memset(&r, 0, sizeof(r));
    r.n_pairs      = n;
    r.threads_used = 1;

    double t_start = omp_get_wtime();

    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    for (int i = 0; i < n; i++) {
        sum_x  += pairs[i].x;
        sum_y  += pairs[i].y;
        sum_xy += pairs[i].x * pairs[i].y;
        sum_x2 += pairs[i].x * pairs[i].x;
        sum_y2 += pairs[i].y * pairs[i].y;
    }

    double denom = sqrt(((double)n * sum_x2 - sum_x * sum_x) *
                        ((double)n * sum_y2 - sum_y * sum_y));
    r.correlation_coefficient = (denom == 0.0) ? 0.0
        : ((double)n * sum_xy - sum_x * sum_y) / denom;

    double bf_denom = (double)n * sum_x2 - sum_x * sum_x;
    r.best_fit_slope     = (bf_denom != 0.0) ? ((double)n * sum_xy - sum_x * sum_y) / bf_denom : 0.0;
    r.best_fit_intercept = (sum_y - r.best_fit_slope * sum_x) / n;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

corr_result_t run_corr_parallel(const score_pair_t *pairs, int n)
{
    corr_result_t r;
    memset(&r, 0, sizeof(r));
    r.n_pairs      = n;
    r.threads_used = omp_get_max_threads();

    double t_start = omp_get_wtime();

    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;

    #pragma omp parallel for \
        reduction(+:sum_x,sum_y,sum_xy,sum_x2,sum_y2) \
        schedule(static)
    for (int i = 0; i < n; i++) {
        sum_x  += pairs[i].x;
        sum_y  += pairs[i].y;
        sum_xy += pairs[i].x * pairs[i].y;
        sum_x2 += pairs[i].x * pairs[i].x;
        sum_y2 += pairs[i].y * pairs[i].y;
    }

    double denom = sqrt(((double)n * sum_x2 - sum_x * sum_x) *
                        ((double)n * sum_y2 - sum_y * sum_y));
    r.correlation_coefficient = (denom == 0.0) ? 0.0
        : ((double)n * sum_xy - sum_x * sum_y) / denom;

    double bf_denom = (double)n * sum_x2 - sum_x * sum_x;
    r.best_fit_slope     = (bf_denom != 0.0) ? ((double)n * sum_xy - sum_x * sum_y) / bf_denom : 0.0;
    r.best_fit_intercept = (sum_y - r.best_fit_slope * sum_x) / n;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* ── POSIX-threads (pthreads) Pearson correlation ──────────────────────── */

#include <pthread.h>

extern int g_num_threads;

typedef struct {
    const score_pair_t *pairs;
    int start, end;
    double sum_x, sum_y, sum_xy, sum_x2, sum_y2;
} corr_thread_data_t;

static void* correlation_worker(void *arg)
{
    corr_thread_data_t *d = (corr_thread_data_t *)arg;
    d->sum_x = d->sum_y = d->sum_xy = d->sum_x2 = d->sum_y2 = 0.0;
    for (int i = d->start; i < d->end; i++) {
        double x = d->pairs[i].x;
        double y = d->pairs[i].y;
        d->sum_x  += x;
        d->sum_y  += y;
        d->sum_xy += x * y;
        d->sum_x2 += x * x;
        d->sum_y2 += y * y;
    }
    return NULL;
}

corr_result_t run_corr_pthread(const score_pair_t *pairs, int n)
{
    corr_result_t r;
    memset(&r, 0, sizeof(r));
    r.n_pairs      = n;
    int T          = g_num_threads;
    r.threads_used = T;

    double t_start = omp_get_wtime();

    corr_thread_data_t *td =
        (corr_thread_data_t *)calloc((size_t)T, sizeof(corr_thread_data_t));
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)T);

    if (!td || !threads) {
        free(td); free(threads);
        r.correlation_coefficient = 0.0;
        return r;
    }

    int chunk = n / T;
    int remainder = n % T;
    int offset = 0;

    for (int i = 0; i < T; i++) {
        int my_n = chunk + (i < remainder ? 1 : 0);
        td[i].pairs = pairs;
        td[i].start = offset;
        td[i].end   = offset + my_n;
        pthread_create(&threads[i], NULL, correlation_worker, &td[i]);
        offset += my_n;
    }
    for (int i = 0; i < T; i++)
        pthread_join(threads[i], NULL);

    /* Reduce */
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    for (int i = 0; i < T; i++) {
        sum_x  += td[i].sum_x;
        sum_y  += td[i].sum_y;
        sum_xy += td[i].sum_xy;
        sum_x2 += td[i].sum_x2;
        sum_y2 += td[i].sum_y2;
    }

    double denom = sqrt(((double)n * sum_x2 - sum_x * sum_x) *
                        ((double)n * sum_y2 - sum_y * sum_y));
    r.correlation_coefficient = (denom == 0.0) ? 0.0
        : ((double)n * sum_xy - sum_x * sum_y) / denom;

    double bf_denom = (double)n * sum_x2 - sum_x * sum_x;
    r.best_fit_slope     = (bf_denom != 0.0) ? ((double)n * sum_xy - sum_x * sum_y) / bf_denom : 0.0;
    r.best_fit_intercept = (sum_y - r.best_fit_slope * sum_x) / n;

    free(td);
    free(threads);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

char* format_corr_json(const corr_result_t *r, const char *label,
                       double db_fetch_ms,
                       const score_pair_t *points, int npts,
                       int total_students, int excluded)
{
    (void)points; (void)npts;   /* data points no longer sent to client */

    char *buf = (char *)malloc(512);
    if (!buf) return NULL;

    snprintf(buf, 512,
        "{\n"
        "    \"mode\": \"%s\",\n"
        "    \"threads_used\": %d,\n"
        "    \"n_pairs\": %d,\n"
        "    \"total_students\": %d,\n"
        "    \"excluded\": %d,\n"
        "    \"elapsed_ms\": %.4f,\n"
        "    \"db_fetch_ms\": %.4f,\n"
        "    \"correlation_coefficient\": %.6f,\n"
        "    \"best_fit_slope\": %.6f,\n"
        "    \"best_fit_intercept\": %.6f\n"
        "  }",
        label,
        r->threads_used,
        r->n_pairs,
        total_students,
        excluded,
        r->elapsed_ms,
        db_fetch_ms,
        r->correlation_coefficient,
        r->best_fit_slope,
        r->best_fit_intercept);

    return buf;
}

char* format_corr_line_json(const corr_result_t *r, const char *label)
{
    char *buf = (char *)malloc(256);
    if (!buf) return NULL;
    snprintf(buf, 256,
        "{\"mode\":\"%s\",\"r\":%.6f,\"elapsed_ms\":%.4f,"
        "\"threads_used\":%d,\"slope\":%.6f,\"intercept\":%.6f}",
        label,
        r->correlation_coefficient,
        r->elapsed_ms,
        r->threads_used,
        r->best_fit_slope,
        r->best_fit_intercept);
    return buf;
}
