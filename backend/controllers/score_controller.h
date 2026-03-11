/*
 * Score Controller
 * Handles /api/seed, /api/calculate/serial|parallel|compare endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef SCORE_CONTROLLER_H
#define SCORE_CONTROLLER_H

#include "../include/civetweb.h"

/* POST /api/seed – seed dummy student/score data into MongoDB */
int SeedHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/serial   – run serial statistical analysis */
int CalcSerialHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/parallel – run OpenMP-parallel statistical analysis */
int CalcParallelHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/calculate/compare  – run both and return a side-by-side comparison */
int CalcCompareHandler(struct mg_connection *conn, void *cbdata);

#endif /* SCORE_CONTROLLER_H */
