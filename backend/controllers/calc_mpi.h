/*
 * MPI Score Calculation Engine
 * Distributed-memory parallel statistical computation using MPI collectives.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CALC_MPI_H
#define CALC_MPI_H

#include "calc_engine.h"   /* calc_result_t, format_result_json */

/* Command codes broadcast from Rank 0 to wake workers */
#define MPI_CMD_SHUTDOWN    0
#define MPI_CMD_CALC_SCORES 1
#define MPI_CMD_CALC_CORR   2

/*
 * Run MPI-distributed score statistics calculation.
 * Called ONLY on Rank 0 (the HTTP server process).
 * Internally broadcasts a command to wake workers, scatters the scores
 * array, performs distributed reductions, and gathers data for median.
 * Returns the same calc_result_t used by run_serial() and run_parallel().
 */
calc_result_t run_mpi(const double *scores, int n, int req_procs);

/*
 * Worker-side handler for MPI_CMD_CALC_SCORES.
 * Called by worker ranks when they receive the command from Rank 0.
 * Participates in Scatterv, Reduce, Bcast, and Gatherv collectives.
 */
void mpi_worker_calc_scores(int rank, int world_size);

#endif /* CALC_MPI_H */
