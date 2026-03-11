/*
 * Data & Test Controller
 * Handles /api/data (generic data playground) and /api/test/* (test scenarios).
 * Copyright (c) 2026
 * MIT License
 */

#ifndef DATA_CONTROLLER_H
#define DATA_CONTROLLER_H

#include "../include/civetweb.h"

/* GET /api/data   – returns sample JSON data
 * POST /api/data  – echoes received payload */
int DataHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/test/{type}  – test scenarios:
 *   performance, error, delay, large-response */
int TestHandler(struct mg_connection *conn, void *cbdata);

#endif /* DATA_CONTROLLER_H */
