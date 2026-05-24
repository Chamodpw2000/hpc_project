/* MPI Score Calculation Engine: distributed stats using MPI collectives. */

#ifndef CALC_MPI_H
#define CALC_MPI_H

#include "calc_engine.h"   /* calc_result_t, format_result_json */

/* Rank 0 command codes to wake workers */
#define MPI_CMD_SHUTDOWN    0
#define MPI_CMD_CALC_SCORES 1
#define MPI_CMD_CALC_CORR   2

/* Runs MPI-distributed score statistics calculation on Rank 0 */
calc_result_t run_mpi(const double *scores, int n, int req_procs);

/* Worker handler for MPI score statistics calculation */
void mpi_worker_calc_scores(int rank, int world_size);

#endif /* CALC_MPI_H */
