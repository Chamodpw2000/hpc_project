/*
 * MPI Correlation Calculation Engine
 * Distributed-memory Pearson correlation using MPI collectives.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CORRELATION_MPI_H
#define CORRELATION_MPI_H

#include "correlation_engine.h"   /* corr_result_t, score_pair_t */

/*
 * Run MPI-distributed Pearson correlation.
 * Called ONLY on Rank 0.
 * Scatters pairs across ranks, each rank computes 5 local sums,
 * then MPI_Reduce aggregates them back to Rank 0.
 */
corr_result_t run_corr_mpi(const score_pair_t *pairs, int n, int req_procs);

/*
 * Worker-side handler for MPI_CMD_CALC_CORR.
 * Participates in Scatterv and Reduce collectives.
 */
void mpi_worker_calc_corr(int rank, int world_size);

#endif /* CORRELATION_MPI_H */
