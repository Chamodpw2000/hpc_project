/*
 * MPI Correlation Calculation Engine — Implementation
 * Distributed Pearson r using a single MPI_Reduce of 5 sums.
 *
 * Communication pattern:
 *   Rank 0 → All:   MPI_Bcast(N)            — number of pairs
 *   Rank 0 → All:   MPI_Scatterv(pairs)     — distribute data
 *   All → Rank 0:   MPI_Reduce(5 sums)      — aggregate partial sums
 *
 * Copyright (c) 2026
 * MIT License
 */

#ifdef ENABLE_MPI

#include "correlation_mpi.h"
#include "calc_mpi.h"   /* MPI_CMD_* constants */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <omp.h>
#include <mpi.h>

/* ── Helper: compute sendcounts and displacements ── */
static void compute_pair_distribution(int n, int world_size,
                                       int *sendcounts, int *displs)
{
    int base  = n / world_size;
    int extra = n % world_size;
    for (int i = 0; i < world_size; i++) {
        sendcounts[i] = base + (i < extra ? 1 : 0);
        displs[i]     = (i == 0) ? 0 : displs[i - 1] + sendcounts[i - 1];
    }
}

/* ── Create an MPI_Datatype for score_pair_t (two contiguous doubles) ── */
static MPI_Datatype create_pair_type(void)
{
    MPI_Datatype pair_type;
    MPI_Type_contiguous(2, MPI_DOUBLE, &pair_type);
    MPI_Type_commit(&pair_type);
    return pair_type;
}

/* ──────────────────────────────────────────────────────────────────────────
 * run_corr_mpi — called ONLY on Rank 0
 * ──────────────────────────────────────────────────────────────────────── */
corr_result_t run_corr_mpi(const score_pair_t *pairs, int n)
{
    corr_result_t r;
    memset(&r, 0, sizeof(r));

    int rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    r.n_pairs      = n;
    r.threads_used = world_size;

    double t_start = omp_get_wtime();

    MPI_Datatype pair_type = create_pair_type();

    /* ── Step 1: Broadcast N ── */
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /* ── Step 2: Compute distribution ── */
    int *sendcounts = (int *)malloc(sizeof(int) * world_size);
    int *displs     = (int *)malloc(sizeof(int) * world_size);
    compute_pair_distribution(n, world_size, sendcounts, displs);

    int local_n = sendcounts[rank];
    score_pair_t *local_pairs = (score_pair_t *)malloc(sizeof(score_pair_t) * local_n);

    /* ── Step 3: Scatter pairs ── */
    MPI_Scatterv(pairs, sendcounts, displs, pair_type,
                 local_pairs, local_n, pair_type,
                 0, MPI_COMM_WORLD);

    /* ── Step 4: Local computation — 5 partial sums ── */
    double local_sums[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < local_n; i++) {
        local_sums[0] += local_pairs[i].x;                        /* sum_x  */
        local_sums[1] += local_pairs[i].y;                        /* sum_y  */
        local_sums[2] += local_pairs[i].x * local_pairs[i].y;     /* sum_xy */
        local_sums[3] += local_pairs[i].x * local_pairs[i].x;     /* sum_x2 */
        local_sums[4] += local_pairs[i].y * local_pairs[i].y;     /* sum_y2 */
    }

    /* ── Step 5: Reduce all 5 sums at once ── */
    double global_sums[5];
    MPI_Reduce(local_sums, global_sums, 5, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    /* ── Step 6: Rank 0 computes Pearson r ── */
    double sum_x  = global_sums[0];
    double sum_y  = global_sums[1];
    double sum_xy = global_sums[2];
    double sum_x2 = global_sums[3];
    double sum_y2 = global_sums[4];

    double denom = sqrt(((double)n * sum_x2 - sum_x * sum_x) *
                        ((double)n * sum_y2 - sum_y * sum_y));
    r.correlation_coefficient = (denom == 0.0) ? 0.0
        : ((double)n * sum_xy - sum_x * sum_y) / denom;

    double bf_denom = (double)n * sum_x2 - sum_x * sum_x;
    r.best_fit_slope     = (bf_denom != 0.0) ? ((double)n * sum_xy - sum_x * sum_y) / bf_denom : 0.0;
    r.best_fit_intercept = (sum_y - r.best_fit_slope * sum_x) / n;

    free(local_pairs);
    free(sendcounts);
    free(displs);
    MPI_Type_free(&pair_type);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* ──────────────────────────────────────────────────────────────────────────
 * mpi_worker_calc_corr — called by worker ranks (rank > 0)
 * ──────────────────────────────────────────────────────────────────────── */
void mpi_worker_calc_corr(int rank, int world_size)
{
    MPI_Datatype pair_type = create_pair_type();

    /* ── Receive N ── */
    int n = 0;
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (n <= 0) {
        MPI_Type_free(&pair_type);
        return;
    }

    /* ── Compute distribution ── */
    int *sendcounts = (int *)malloc(sizeof(int) * world_size);
    int *displs     = (int *)malloc(sizeof(int) * world_size);
    compute_pair_distribution(n, world_size, sendcounts, displs);

    int local_n = sendcounts[rank];
    score_pair_t *local_pairs = (score_pair_t *)malloc(sizeof(score_pair_t) * local_n);

    /* ── Receive scattered chunk ── */
    MPI_Scatterv(NULL, sendcounts, displs, pair_type,
                 local_pairs, local_n, pair_type,
                 0, MPI_COMM_WORLD);

    /* ── Local computation ── */
    double local_sums[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < local_n; i++) {
        local_sums[0] += local_pairs[i].x;
        local_sums[1] += local_pairs[i].y;
        local_sums[2] += local_pairs[i].x * local_pairs[i].y;
        local_sums[3] += local_pairs[i].x * local_pairs[i].x;
        local_sums[4] += local_pairs[i].y * local_pairs[i].y;
    }

    /* ── Reduce ── */
    double global_sums[5];
    MPI_Reduce(local_sums, global_sums, 5, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    free(local_pairs);
    free(sendcounts);
    free(displs);
    MPI_Type_free(&pair_type);
}

#endif /* ENABLE_MPI */
