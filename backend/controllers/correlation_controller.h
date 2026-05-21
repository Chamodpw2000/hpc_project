/*
 * Correlation Controller
 * HTTP handlers for /api/calculate/correlation/serial|parallel|compare.
 * Query params: subject1, class1, subject2, class2
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CORRELATION_CONTROLLER_H
#define CORRELATION_CONTROLLER_H

#include "../include/civetweb.h"

/* GET /api/calculate/correlation/serial   */
int CorrSerialHandler  (struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/correlation/parallel */
int CorrParallelHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/correlation/compare  */
int CorrCompareHandler (struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/correlation/pthread  */
int CorrPthreadHandler (struct mg_connection *conn, void *cbdata);

#ifdef ENABLE_MPI
/* GET /api/calculate/correlation/mpi – MPI-distributed Pearson correlation */
int CorrMpiHandler(struct mg_connection *conn, void *cbdata);
#endif

#endif /* CORRELATION_CONTROLLER_H */
