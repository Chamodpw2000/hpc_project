/*
 * MPI Worker Loop — Implementation
 * Worker ranks sit blocked on MPI_Bcast until Rank 0 sends a command.
 *
 * Commands:
 *   MPI_CMD_SHUTDOWN    (0) — exit the loop
 *   MPI_CMD_CALC_SCORES (1) — participate in score statistics reduction
 *   MPI_CMD_CALC_CORR   (2) — participate in correlation reduction
 *
 * Copyright (c) 2026
 * MIT License
 */

#ifdef ENABLE_MPI

#include "mpi_worker.h"
#include "calc_mpi.h"          /* MPI_CMD_* constants, mpi_worker_calc_scores */
#include "correlation_mpi.h"   /* mpi_worker_calc_corr */

#include <stdio.h>
#include <mpi.h>

void mpi_worker_loop(int rank, int world_size)
{
    printf("[MPI Worker %d/%d] Ready and waiting for commands\n",
           rank, world_size);

    while (1) {
        int cmd = 0;

        /* Block until Rank 0 broadcasts a command */
        MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (cmd == MPI_CMD_SHUTDOWN) {
            printf("[MPI Worker %d] Shutdown received, exiting\n", rank);
            break;
        }

        if (cmd == MPI_CMD_CALC_SCORES) {
            mpi_worker_calc_scores(rank, world_size);
        }
        else if (cmd == MPI_CMD_CALC_CORR) {
            mpi_worker_calc_corr(rank, world_size);
        }
        else {
            printf("[MPI Worker %d] Unknown command: %d\n", rank, cmd);
        }
    }
}

#endif /* ENABLE_MPI */
