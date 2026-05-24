/*
 * MPI Worker Loop
 * Daemon loop for worker ranks (rank > 0).
 * Workers block on MPI_Bcast waiting for commands from Rank 0.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef MPI_WORKER_H
#define MPI_WORKER_H

/* Enter the main worker node communication loop (blocks waiting for Rank 0 broadcasts) */
void mpi_worker_loop(int rank, int world_size);

#endif /* MPI_WORKER_H */
