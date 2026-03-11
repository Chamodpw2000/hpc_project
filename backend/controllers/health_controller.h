/*
 * Health Controller
 * Handles GET / (welcome) and GET /health endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef HEALTH_CONTROLLER_H
#define HEALTH_CONTROLLER_H

#include "../include/civetweb.h"

/* GET /  – welcome page */
int RootHandler(struct mg_connection *conn, void *cbdata);

/* GET /health  – server liveness check */
int HealthHandler(struct mg_connection *conn, void *cbdata);

#endif /* HEALTH_CONTROLLER_H */
