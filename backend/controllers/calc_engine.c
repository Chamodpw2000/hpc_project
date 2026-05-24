#include "calc_engine.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <pthread.h>

extern int g_num_threads;

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

calc_result_t run_serial(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count       = n;
    r.threads_used = 1;

    double t_start = omp_get_wtime();

    r.min = scores[0];
    r.max = scores[0];
    r.sum = 0.0;
    for (int i = 0; i < n; i++) {
        r.sum += scores[i];
        if (scores[i] < r.min) r.min = scores[i];
        if (scores[i] > r.max) r.max = scores[i];
    }
    r.mean = r.sum / n;

    double var_sum = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if      (scores[i] >= 75) r.grade_A++;
        else if (scores[i] >= 65) r.grade_B++;
        else if (scores[i] >= 55) r.grade_C++;
        else if (scores[i] >= 45) r.grade_D++;
        else                      r.grade_F++;
    }
    r.variance = var_sum / n;
    r.stddev   = sqrt(r.variance);

    double *sorted = (double *)malloc(sizeof(double) * (size_t)n);
    memcpy(sorted, scores, sizeof(double) * (size_t)n);

    double sort_start = omp_get_wtime();
    qsort(sorted, (size_t)n, sizeof(double), cmp_double);
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    r.median = (n % 2 == 0)
        ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
        :  sorted[n / 2];
    free(sorted);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

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
        #pragma omp task shared(arr) if (depth < 4)
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

calc_result_t run_parallel(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count        = n;
    r.threads_used = omp_get_max_threads();

    double t_start = omp_get_wtime();

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

    double var_sum = 0.0;
    int gA = 0, gB = 0, gC = 0, gD = 0, gF = 0;
    #pragma omp parallel for reduction(+:var_sum,gA,gB,gC,gD,gF) schedule(static)
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if      (scores[i] >= 75) gA++;
        else if (scores[i] >= 65) gB++;
        else if (scores[i] >= 55) gC++;
        else if (scores[i] >= 45) gD++;
        else                      gF++;
    }
    r.variance = var_sum / n;
    r.stddev   = sqrt(r.variance);
    r.grade_A = gA; r.grade_B = gB; r.grade_C = gC;
    r.grade_D = gD; r.grade_F = gF;

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

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* Merge two sorted arrays into a new heap-allocated buffer */
double* merge_sorted_arrays(const double *a, int na,
                            const double *b, int nb)
{
    double *res = (double *)malloc(sizeof(double) * (size_t)(na + nb));
    if (!res) return NULL;
    int i = 0, j = 0, k = 0;
    while (i < na && j < nb)
        res[k++] = (a[i] <= b[j]) ? a[i++] : b[j++];
    while (i < na) res[k++] = a[i++];
    while (j < nb) res[k++] = b[j++];
    return res;
}

/* Thread argument structures for statistical computations */

typedef struct {
    const double *scores;
    int start, end;
    double local_sum, local_min, local_max;
    int grade_a, grade_b, grade_c, grade_d, grade_f;
} calc_thread_data_t;

typedef struct {
    const double *scores;
    int start, end;
    double mean;
    double local_var_sum;
} calc_var_thread_data_t;

typedef struct {
    const double *scores; /* source — read only                        */
    double *sorted_buf;   /* private copy — worker allocates and sorts */
    int start, n;
} calc_sort_thread_data_t;

/* Thread worker functions */

static void* calc_stats_worker(void *arg)
{
    calc_thread_data_t *d = (calc_thread_data_t *)arg;
    d->local_sum = 0.0;
    d->local_min = d->scores[d->start];
    d->local_max = d->scores[d->start];
    d->grade_a = d->grade_b = d->grade_c = d->grade_d = d->grade_f = 0;

    for (int i = d->start; i < d->end; i++) {
        double s = d->scores[i];
        d->local_sum += s;
        if (s < d->local_min) d->local_min = s;
        if (s > d->local_max) d->local_max = s;
        if      (s >= 75) d->grade_a++;
        else if (s >= 65) d->grade_b++;
        else if (s >= 55) d->grade_c++;
        else if (s >= 45) d->grade_d++;
        else              d->grade_f++;
    }
    return NULL;
}

static void* calc_var_worker(void *arg)
{
    calc_var_thread_data_t *d = (calc_var_thread_data_t *)arg;
    d->local_var_sum = 0.0;
    for (int i = d->start; i < d->end; i++) {
        double diff = d->scores[i] - d->mean;
        d->local_var_sum += diff * diff;
    }
    return NULL;
}

static void* calc_sort_worker(void *arg)
{
    calc_sort_thread_data_t *d = (calc_sort_thread_data_t *)arg;
    d->sorted_buf = (double *)malloc(sizeof(double) * (size_t)d->n);
    if (d->sorted_buf) {
        memcpy(d->sorted_buf, d->scores + d->start,
               sizeof(double) * (size_t)d->n);
        qsort(d->sorted_buf, (size_t)d->n, sizeof(double), cmp_double);
    }
    return NULL;
}

/* POSIX-threads parallelized score statistics calculation */
calc_result_t run_pthread(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count        = n;
    int T          = g_num_threads;
    r.threads_used = T;

    double t_start = omp_get_wtime();

    /* Allocate per-thread data */
    calc_thread_data_t     *stats_data = (calc_thread_data_t *)     calloc((size_t)T, sizeof(calc_thread_data_t));
    calc_sort_thread_data_t *sort_data = (calc_sort_thread_data_t *)calloc((size_t)T, sizeof(calc_sort_thread_data_t));
    pthread_t              *threads    = (pthread_t *)              malloc(sizeof(pthread_t) * (size_t)(2 * T));

    if (!stats_data || !sort_data || !threads) {
        free(stats_data); free(sort_data); free(threads);
        r.count = -1;
        return r;
    }

    int chunk = n / T;
    int remainder = n % T;

    /* Phase 1: spawn stats workers and sort workers simultaneously */
    int offset = 0;
    for (int i = 0; i < T; i++) {
        int my_n = chunk + (i < remainder ? 1 : 0);
        stats_data[i].scores = scores;
        stats_data[i].start  = offset;
        stats_data[i].end    = offset + my_n;

        sort_data[i].scores  = scores;
        sort_data[i].start   = offset;
        sort_data[i].n       = my_n;
        sort_data[i].sorted_buf = NULL;

        pthread_create(&threads[i],     NULL, calc_stats_worker, &stats_data[i]);
        pthread_create(&threads[T + i], NULL, calc_sort_worker,  &sort_data[i]);
        offset += my_n;
    }

    /* Join all 2T threads */
    for (int i = 0; i < 2 * T; i++)
        pthread_join(threads[i], NULL);

    /* Check if any sort worker failed to allocate */
    for (int i = 0; i < T; i++) {
        if (!sort_data[i].sorted_buf) {
            for (int j = 0; j < T; j++) free(sort_data[j].sorted_buf);
            free(stats_data); free(sort_data); free(threads);
            r.count = -1;
            return r;
        }
    }

    /* Reduce stats */
    r.sum = 0.0;
    r.min = stats_data[0].local_min;
    r.max = stats_data[0].local_max;
    r.grade_A = r.grade_B = r.grade_C = r.grade_D = r.grade_F = 0;
    for (int i = 0; i < T; i++) {
        r.sum += stats_data[i].local_sum;
        if (stats_data[i].local_min < r.min) r.min = stats_data[i].local_min;
        if (stats_data[i].local_max > r.max) r.max = stats_data[i].local_max;
        r.grade_A += stats_data[i].grade_a;
        r.grade_B += stats_data[i].grade_b;
        r.grade_C += stats_data[i].grade_c;
        r.grade_D += stats_data[i].grade_d;
        r.grade_F += stats_data[i].grade_f;
    }
    r.mean = r.sum / n;

    /* Phase 2: spawn variance workers (requires mean from Phase 1) */
    calc_var_thread_data_t *var_data =
        (calc_var_thread_data_t *)calloc((size_t)T, sizeof(calc_var_thread_data_t));
    if (!var_data) {
        for (int i = 0; i < T; i++) free(sort_data[i].sorted_buf);
        free(stats_data); free(sort_data); free(threads);
        r.count = -1;
        return r;
    }

    offset = 0;
    for (int i = 0; i < T; i++) {
        int my_n = chunk + (i < remainder ? 1 : 0);
        var_data[i].scores = scores;
        var_data[i].start  = offset;
        var_data[i].end    = offset + my_n;
        var_data[i].mean   = r.mean;
        pthread_create(&threads[i], NULL, calc_var_worker, &var_data[i]);
        offset += my_n;
    }
    for (int i = 0; i < T; i++)
        pthread_join(threads[i], NULL);

    double var_sum = 0.0;
    for (int i = 0; i < T; i++)
        var_sum += var_data[i].local_var_sum;
    r.variance = var_sum / n;
    r.stddev   = sqrt(r.variance);

    free(var_data);

    /* Hierarchical merge of sorted chunks for median calculation */
    double sort_start = omp_get_wtime();

    /* Merge sorted sub-arrays across threads */
    double *sorted_final = sort_data[0].sorted_buf;
    int sorted_n         = sort_data[0].n;

    for (int i = 1; i < T; i++) {
        double *next = merge_sorted_arrays(sorted_final, sorted_n,
                                           sort_data[i].sorted_buf,
                                           sort_data[i].n);
        if (!next) {
            /* Allocation failed — clean up everything */
            if (i > 1) free(sorted_final); /* free previous intermediate */
            for (int j = 0; j < T; j++) free(sort_data[j].sorted_buf);
            free(stats_data); free(sort_data); free(threads);
            r.count = -1;
            return r;
        }
        if (i > 1) free(sorted_final); /* free previous intermediate */
        sorted_final = next;
        sorted_n += sort_data[i].n;
    }

    r.median = (sorted_n % 2 == 0)
        ? (sorted_final[sorted_n / 2 - 1] + sorted_final[sorted_n / 2]) / 2.0
        :  sorted_final[sorted_n / 2];

    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    /* Free the final merged buffer (only exists when T > 1) */
    if (T > 1) free(sorted_final);
    /* Free all worker-allocated sort buffers (includes sort_data[0]) */
    for (int i = 0; i < T; i++) free(sort_data[i].sorted_buf);

    free(stats_data);
    free(sort_data);
    free(threads);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

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
