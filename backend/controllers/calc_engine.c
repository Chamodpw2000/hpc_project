/*
 * Score Calculation Engine Implementation
 * Serial and OpenMP-parallel statistical computation.
 * Copyright (c) 2026
 * MIT License
 */

#include "calc_engine.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <stdio.h>

/* ---- qsort comparator -------------------------------------------------- */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ==========================================================================
 * SERIAL calculation
 * ========================================================================== */
calc_result_t run_serial(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count       = n;
    r.threads_used = 1;

    double t_start = omp_get_wtime();

    /* Sum, min, max */
    r.min = scores[0];
    r.max = scores[0];
    r.sum = 0.0;
    for (int i = 0; i < n; i++) {
        r.sum += scores[i];
        if (scores[i] < r.min) r.min = scores[i];
        if (scores[i] > r.max) r.max = scores[i];
    }
    r.mean = r.sum / n;

    /* Variance + grade distribution */
    double var_sum = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if      (scores[i] >= 90) r.grade_A++;
        else if (scores[i] >= 80) r.grade_B++;
        else if (scores[i] >= 70) r.grade_C++;
        else if (scores[i] >= 60) r.grade_D++;
        else                      r.grade_F++;
    }
    r.variance = var_sum / n;
    r.stddev   = sqrt(r.variance);

    /* Median via sorted copy */
    double *sorted = (double *)malloc(sizeof(double) * (size_t)n);
    memcpy(sorted, scores, sizeof(double) * (size_t)n);

    double sort_start = omp_get_wtime();
    qsort(sorted, (size_t)n, sizeof(double), cmp_double);
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    r.median = (n % 2 == 0)
        ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
        :  sorted[n / 2];
    free(sorted);

    /* Intensive CPU work to amplify serial/parallel difference */
    volatile double dummy = 0.0;
    for (int rep = 0; rep < 50; rep++)
        for (int i = 0; i < n; i++)
            dummy += sin(scores[i]) * cos(scores[i]);
    (void)dummy;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* ==========================================================================
 * PARALLEL (OpenMP) helpers – merge sort
 * ========================================================================== */
static void merge_arrays(double *arr, int l, int m, int r)
{
    int     n1 = m - l + 1, n2 = r - m;
    double *L  = malloc(sizeof(double) * (size_t)n1);
    double *R  = malloc(sizeof(double) * (size_t)n2);
    memcpy(L, arr + l,     sizeof(double) * (size_t)n1);
    memcpy(R, arr + m + 1, sizeof(double) * (size_t)n2);

    int i = 0, j = 0, k = l;
    while (i < n1 && j < n2) arr[k++] = (L[i] <= R[j]) ? L[i++] : R[j++];
    while (i < n1)            arr[k++] = L[i++];
    while (j < n2)            arr[k++] = R[j++];
    free(L);
    free(R);
}

static void parallel_merge_sort(double *arr, int l, int r, int depth)
{
    if (l >= r) return;
    int m = l + (r - l) / 2;

    if (depth < 4) {
        #pragma omp task shared(arr) if(depth < 4)
        parallel_merge_sort(arr, l, m, depth + 1);
        #pragma omp task shared(arr) if(depth < 4)
        parallel_merge_sort(arr, m + 1, r, depth + 1);
        #pragma omp taskwait
    } else {
        parallel_merge_sort(arr, l, m, depth + 1);
        parallel_merge_sort(arr, m + 1, r, depth + 1);
    }
    merge_arrays(arr, l, m, r);
}

/* ==========================================================================
 * PARALLEL calculation
 * ========================================================================== */
calc_result_t run_parallel(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count        = n;
    r.threads_used = omp_get_max_threads();

    double t_start = omp_get_wtime();

    /* Parallel sum, min, max */
    double p_sum = 0.0, p_min = scores[0], p_max = scores[0];
    #pragma omp parallel for reduction(+:p_sum) reduction(min:p_min) reduction(max:p_max) schedule(static)
    for (int i = 0; i < n; i++) {
        p_sum += scores[i];
        if (scores[i] < p_min) p_min = scores[i];
        if (scores[i] > p_max) p_max = scores[i];
    }
    r.sum  = p_sum;
    r.min  = p_min;
    r.max  = p_max;
    r.mean = r.sum / n;

    /* Parallel variance + grade distribution */
    double var_sum = 0.0;
    int gA = 0, gB = 0, gC = 0, gD = 0, gF = 0;
    #pragma omp parallel for reduction(+:var_sum,gA,gB,gC,gD,gF) schedule(static)
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if      (scores[i] >= 90) gA++;
        else if (scores[i] >= 80) gB++;
        else if (scores[i] >= 70) gC++;
        else if (scores[i] >= 60) gD++;
        else                      gF++;
    }
    r.variance = var_sum / n;
    r.stddev   = sqrt(r.variance);
    r.grade_A = gA; r.grade_B = gB; r.grade_C = gC;
    r.grade_D = gD; r.grade_F = gF;

    /* Parallel merge-sort for median */
    double *sorted = (double *)malloc(sizeof(double) * (size_t)n);
    memcpy(sorted, scores, sizeof(double) * (size_t)n);

    double sort_start = omp_get_wtime();
    #pragma omp parallel
    {
        #pragma omp single
        parallel_merge_sort(sorted, 0, n - 1, 0);
    }
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    r.median = (n % 2 == 0)
        ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
        :  sorted[n / 2];
    free(sorted);

    /* Intensive CPU work – parallelised */
    double local_dummy = 0.0;
    #pragma omp parallel for reduction(+:local_dummy) schedule(static)
    for (int rep = 0; rep < 50; rep++)
        for (int i = 0; i < n; i++)
            local_dummy += sin(scores[i]) * cos(scores[i]);
    (void)local_dummy;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* ==========================================================================
 * JSON serialiser
 * ========================================================================== */
void format_result_json(char *buf, size_t sz,
                        const calc_result_t *r, const char *label,
                        double db_fetch_ms)
{
    snprintf(buf, sz,
        "{\n"
        "    \"mode\": \"%s\",\n"
        "    \"threads_used\": %d,\n"
        "    \"scores_count\": %d,\n"
        "    \"elapsed_ms\": %.4f,\n"
        "    \"sort_time_ms\": %.4f,\n"
        "    \"db_fetch_ms\": %.4f,\n"
        "    \"statistics\": {\n"
        "      \"sum\": %.4f,\n"
        "      \"mean\": %.4f,\n"
        "      \"median\": %.4f,\n"
        "      \"variance\": %.4f,\n"
        "      \"stddev\": %.4f,\n"
        "      \"min\": %.4f,\n"
        "      \"max\": %.4f\n"
        "    },\n"
        "    \"grade_distribution\": {\n"
        "      \"A_90_100\": %d,\n"
        "      \"B_80_89\": %d,\n"
        "      \"C_70_79\": %d,\n"
        "      \"D_60_69\": %d,\n"
        "      \"F_below_60\": %d\n"
        "    }\n"
        "  }",
        label, r->threads_used, r->count, r->elapsed_ms, r->sort_time_ms,
        db_fetch_ms,
        r->sum, r->mean, r->median, r->variance, r->stddev, r->min, r->max,
        r->grade_A, r->grade_B, r->grade_C, r->grade_D, r->grade_F);
}
