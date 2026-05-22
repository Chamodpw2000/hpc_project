/*
 * MPI Score Calculation Engine — Implementation
 * Distributed-memory parallel statistical computation using MPI collectives.
 *
 * Communication pattern per API request:
 *   Rank 0 → All:   MPI_Bcast(use_size)          — effective process count
 *   Rank 0 → All:   MPI_Bcast(N)                 — dataset size
 *   Rank 0 → All:   MPI_Scatterv(scores)         — distribute data
 *   All → Rank 0:   MPI_Reduce(sum, min, max)    — phase 1 stats
 *   Rank 0 → All:   MPI_Bcast(mean)              — share global mean
 *   All → Rank 0:   MPI_Reduce(var, grades)      — phase 2 stats
 *   All → Rank 0:   MPI_Gatherv(local_scores)    — for median sort
 *
 * req_procs allows using fewer processes than world_size via MPI_Comm_split.
 *
 * Copyright (c) 2026
 * MIT License
 */

#ifdef ENABLE_MPI

#include "calc_mpi.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <omp.h>
#include <mpi.h>

/* ── Helper: compute sendcounts and displacements for Scatterv/Gatherv ── */
static void compute_distribution(int n, int world_size,
                                  int *sendcounts, int *displs)
{
    int base  = n / world_size;
    int extra = n % world_size;
    for (int i = 0; i < world_size; i++) {
        sendcounts[i] = base + (i < extra ? 1 : 0);
        displs[i]     = (i == 0) ? 0 : displs[i - 1] + sendcounts[i - 1];
    }
}

/* ── Helper: comparison function for qsort (ascending doubles) ── */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ──────────────────────────────────────────────────────────────────────────
 * run_mpi — called ONLY on Rank 0 (the HTTP server process)
 * req_procs: requested process count (0 = use all available)
 * ──────────────────────────────────────────────────────────────────────── */
calc_result_t run_mpi(const double *scores, int n, int req_procs)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    /* ── Determine effective process count ── */
    int use_size = (req_procs > 0 && req_procs <= world_size) ? req_procs : world_size;
    MPI_Bcast(&use_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ── Split communicator — excluded ranks receive MPI_COMM_NULL ── */
    int color = (rank < use_size) ? 0 : MPI_UNDEFINED;
    MPI_Comm sub_comm;
    MPI_Comm_split(MPI_COMM_WORLD, color, rank, &sub_comm);

    r.count        = n;
    r.threads_used = use_size;

    double t_start = omp_get_wtime();

    /* ── Step 1: Broadcast N to all sub_comm ranks ── */
    MPI_Bcast(&n, 1, MPI_INT, 0, sub_comm);

    /* ── Step 2: Compute chunk distribution ── */
    int *sendcounts = (int *)malloc(sizeof(int) * use_size);
    int *displs     = (int *)malloc(sizeof(int) * use_size);
    compute_distribution(n, use_size, sendcounts, displs);

    int sub_rank;
    MPI_Comm_rank(sub_comm, &sub_rank);
    int local_n = sendcounts[sub_rank];
    double *local_scores = (double *)malloc(sizeof(double) * local_n);

    /* ── Step 3: Scatter scores to all sub_comm ranks ── */
    MPI_Scatterv(scores, sendcounts, displs, MPI_DOUBLE,
                 local_scores, local_n, MPI_DOUBLE,
                 0, sub_comm);

    /* ── Step 4: Phase 1 — local sum, min, max ── */
    double local_sum = 0.0;
    double local_min = local_scores[0];
    double local_max = local_scores[0];

    for (int i = 0; i < local_n; i++) {
        local_sum += local_scores[i];
        if (local_scores[i] < local_min) local_min = local_scores[i];
        if (local_scores[i] > local_max) local_max = local_scores[i];
    }

    double global_sum, global_min, global_max;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, 0, sub_comm);
    MPI_Reduce(&local_min, &global_min, 1, MPI_DOUBLE, MPI_MIN, 0, sub_comm);
    MPI_Reduce(&local_max, &global_max, 1, MPI_DOUBLE, MPI_MAX, 0, sub_comm);

    r.sum  = global_sum;
    r.min  = global_min;
    r.max  = global_max;
    r.mean = global_sum / n;

    /* ── Step 5: Broadcast global mean ── */
    double global_mean = r.mean;
    MPI_Bcast(&global_mean, 1, MPI_DOUBLE, 0, sub_comm);

    /* ── Step 6: Phase 2 — local variance and grade distribution ── */
    double local_var_sum = 0.0;
    int local_grades[5] = {0, 0, 0, 0, 0};

    for (int i = 0; i < local_n; i++) {
        double diff = local_scores[i] - global_mean;
        local_var_sum += diff * diff;

        if      (local_scores[i] >= 75) local_grades[0]++;
        else if (local_scores[i] >= 65) local_grades[1]++;
        else if (local_scores[i] >= 55) local_grades[2]++;
        else if (local_scores[i] >= 45) local_grades[3]++;
        else                            local_grades[4]++;
    }

    double global_var_sum;
    int global_grades[5];
    MPI_Reduce(&local_var_sum, &global_var_sum, 1, MPI_DOUBLE, MPI_SUM, 0, sub_comm);
    MPI_Reduce(local_grades, global_grades, 5, MPI_INT, MPI_SUM, 0, sub_comm);

    r.variance = global_var_sum / n;
    r.stddev   = sqrt(r.variance);
    r.grade_A  = global_grades[0];
    r.grade_B  = global_grades[1];
    r.grade_C  = global_grades[2];
    r.grade_D  = global_grades[3];
    r.grade_F  = global_grades[4];

    /* ── Step 7: Dummy load (embarrassingly parallel) ── */
    volatile double dummy = 0.0;
    for (int rep = 0; rep < 50; rep++)
        for (int i = 0; i < local_n; i++)
            dummy += sin(local_scores[i]) * cos(local_scores[i]);
    (void)dummy;

    /* ── Step 8: Gather all scores back to sub_rank 0 for median sort ── */
    double *gathered = NULL;
    if (sub_rank == 0) {
        gathered = (double *)malloc(sizeof(double) * n);
    }
    MPI_Gatherv(local_scores, local_n, MPI_DOUBLE,
                gathered, sendcounts, displs, MPI_DOUBLE,
                0, sub_comm);

    /* ── Step 9: sub_rank 0 sorts and picks median ── */
    if (sub_rank == 0 && gathered) {
        double sort_start = omp_get_wtime();
        qsort(gathered, (size_t)n, sizeof(double), cmp_double);
        r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

        r.median = (n % 2 == 0)
            ? (gathered[n / 2 - 1] + gathered[n / 2]) / 2.0
            :  gathered[n / 2];
        free(gathered);
    }

    free(local_scores);
    free(sendcounts);
    free(displs);
    MPI_Comm_free(&sub_comm);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* ──────────────────────────────────────────────────────────────────────────
 * mpi_worker_calc_scores — called by worker ranks (rank > 0)
 * Receives use_size, joins sub-communicator (or skips if excluded),
 * then participates in the same collective operations as run_mpi().
 * ──────────────────────────────────────────────────────────────────────── */
void mpi_worker_calc_scores(int rank, int world_size)
{
    (void)world_size;

    /* ── Receive effective process count ── */
    int use_size = 0;
    MPI_Bcast(&use_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int color = (rank < use_size) ? 0 : MPI_UNDEFINED;
    MPI_Comm sub_comm;
    MPI_Comm_split(MPI_COMM_WORLD, color, rank, &sub_comm);

    if (sub_comm == MPI_COMM_NULL) return;  /* excluded from this run */

    int sub_rank;
    MPI_Comm_rank(sub_comm, &sub_rank);

    /* ── Step 1: Receive N ── */
    int n = 0;
    MPI_Bcast(&n, 1, MPI_INT, 0, sub_comm);

    if (n <= 0) {
        MPI_Comm_free(&sub_comm);
        return;
    }

    /* ── Step 2: Compute chunk distribution ── */
    int *sendcounts = (int *)malloc(sizeof(int) * use_size);
    int *displs     = (int *)malloc(sizeof(int) * use_size);
    compute_distribution(n, use_size, sendcounts, displs);

    int local_n = sendcounts[sub_rank];
    double *local_scores = (double *)malloc(sizeof(double) * local_n);

    /* ── Step 3: Receive scattered chunk ── */
    MPI_Scatterv(NULL, sendcounts, displs, MPI_DOUBLE,
                 local_scores, local_n, MPI_DOUBLE,
                 0, sub_comm);

    /* ── Step 4: Phase 1 — local sum, min, max ── */
    double local_sum = 0.0;
    double local_min = local_scores[0];
    double local_max = local_scores[0];

    for (int i = 0; i < local_n; i++) {
        local_sum += local_scores[i];
        if (local_scores[i] < local_min) local_min = local_scores[i];
        if (local_scores[i] > local_max) local_max = local_scores[i];
    }

    double global_sum, global_min, global_max;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, 0, sub_comm);
    MPI_Reduce(&local_min, &global_min, 1, MPI_DOUBLE, MPI_MIN, 0, sub_comm);
    MPI_Reduce(&local_max, &global_max, 1, MPI_DOUBLE, MPI_MAX, 0, sub_comm);

    /* ── Step 5: Receive global mean ── */
    double global_mean = 0.0;
    MPI_Bcast(&global_mean, 1, MPI_DOUBLE, 0, sub_comm);

    /* ── Step 6: Phase 2 — local variance and grades ── */
    double local_var_sum = 0.0;
    int local_grades[5] = {0, 0, 0, 0, 0};

    for (int i = 0; i < local_n; i++) {
        double diff = local_scores[i] - global_mean;
        local_var_sum += diff * diff;

        if      (local_scores[i] >= 75) local_grades[0]++;
        else if (local_scores[i] >= 65) local_grades[1]++;
        else if (local_scores[i] >= 55) local_grades[2]++;
        else if (local_scores[i] >= 45) local_grades[3]++;
        else                            local_grades[4]++;
    }

    double global_var_sum;
    int global_grades[5];
    MPI_Reduce(&local_var_sum, &global_var_sum, 1, MPI_DOUBLE, MPI_SUM, 0, sub_comm);
    MPI_Reduce(local_grades, global_grades, 5, MPI_INT, MPI_SUM, 0, sub_comm);

    /* ── Step 7: Dummy load ── */
    volatile double dummy = 0.0;
    for (int rep = 0; rep < 50; rep++)
        for (int i = 0; i < local_n; i++)
            dummy += sin(local_scores[i]) * cos(local_scores[i]);
    (void)dummy;

    /* ── Step 8: Send local data back for median ── */
    MPI_Gatherv(local_scores, local_n, MPI_DOUBLE,
                NULL, sendcounts, displs, MPI_DOUBLE,
                0, sub_comm);

    free(local_scores);
    free(sendcounts);
    free(displs);
    MPI_Comm_free(&sub_comm);
}

#endif /* ENABLE_MPI */
