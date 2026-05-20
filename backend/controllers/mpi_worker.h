/*
 * MPI Worker Loop
 * Daemon loop for worker ranks (rank > 0).
 * Workers block on MPI_Bcast waiting for commands from Rank 0.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef MPI_WORKER_H
#define MPI_WORKER_H

/*
 * Enter the worker loop.  Workers sit idle until Rank 0 broadcasts
 * a command (MPI_CMD_CALC_SCORES, MPI_CMD_CALC_CORR, or MPI_CMD_SHUTDOWN).
 * On MPI_CMD_SHUTDOWN the function returns and the process can finalize.
 */
void mpi_worker_loop(int rank, int world_size);

#endif /* MPI_WORKER_H */
