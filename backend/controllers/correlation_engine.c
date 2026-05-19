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
#include <time.h>

#define GRAPH_SAMPLE_SIZE 1000

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

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

char* format_corr_json(const corr_result_t *r, const char *label,
                       double db_fetch_ms,
                       const score_pair_t *points, int npts)
{
    /* ── Reservoir sampling (Algorithm R) ──────────────────────────────────
     * All npts pairs are used for the Pearson calculation above.
     * For the scatter graph we only send at most GRAPH_SAMPLE_SIZE points
     * so the frontend payload stays small regardless of dataset size.
     * n_pairs in the JSON still reflects the true count used for statistics. */
    int sample_n = npts < GRAPH_SAMPLE_SIZE ? npts : GRAPH_SAMPLE_SIZE;

    int *idx = (int *)malloc((size_t)sample_n * sizeof(int));
    if (!idx) return NULL;

    /* Fill reservoir with the first sample_n indices */
    for (int i = 0; i < sample_n; i++) idx[i] = i;

    if (npts > GRAPH_SAMPLE_SIZE) {
        /* For each remaining element, replace a reservoir slot with
         * probability GRAPH_SAMPLE_SIZE / (i+1) — gives uniform sample */
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)points;
        for (int i = GRAPH_SAMPLE_SIZE; i < npts; i++) {
            int j = (int)((unsigned long)rand_r(&seed) % (unsigned long)(i + 1));
            if (j < GRAPH_SAMPLE_SIZE) idx[j] = i;
        }
    }

    /* ~400 bytes fixed + ~30 bytes per sampled point */
    size_t buf_size = 512 + (size_t)sample_n * 32;
    char  *buf      = (char *)malloc(buf_size);
    if (!buf) { free(idx); return NULL; }

    int pos = snprintf(buf, buf_size,
        "{\n"
        "    \"mode\": \"%s\",\n"
        "    \"threads_used\": %d,\n"
        "    \"n_pairs\": %d,\n"
        "    \"elapsed_ms\": %.4f,\n"
        "    \"db_fetch_ms\": %.4f,\n"
        "    \"correlation_coefficient\": %.6f,\n"
        "    \"data_points\": [",
        label,
        r->threads_used,
        r->n_pairs,          /* true count, not the sample size */
        r->elapsed_ms,
        db_fetch_ms,
        r->correlation_coefficient);

    if (pos < 0 || (size_t)pos >= buf_size) { free(buf); free(idx); return NULL; }

    for (int i = 0; i < sample_n; i++) {
        int written = snprintf(buf + pos, buf_size - (size_t)pos,
            "%s{\"x\":%.2f,\"y\":%.2f}",
            (i > 0 ? "," : ""),
            points[idx[i]].x, points[idx[i]].y);
        if (written < 0 || (size_t)pos + (size_t)written >= buf_size - 8) break;
        pos += written;
    }

    int tail = snprintf(buf + pos, buf_size - (size_t)pos, "]\n  }");
    if (tail > 0) pos += tail;
    (void)pos;

    free(idx);
    return buf;
}
