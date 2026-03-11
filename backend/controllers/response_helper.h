/*
 * HTTP Response Helper
 * Shared JSON and error response utilities for all controllers.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef RESPONSE_HELPER_H
#define RESPONSE_HELPER_H

#include "../include/civetweb.h"

/* Shared counters - defined in score_analyzer_backend.c */
extern unsigned requestCounter;

/*
 * Send a standard JSON success response.
 *   status  – e.g. "success", "running", "healthy"
 *   message – human-readable message
 *   data    – raw JSON fragment to embed under "data" key (may be NULL)
 * Returns HTTP status code (200).
 */
int SendJSONResponse(struct mg_connection *conn,
                     const char *status,
                     const char *message,
                     const char *data);

/*
 * Send a JSON error response with the given HTTP status code.
 * Returns the status_code passed in.
 */
int SendErrorResponse(struct mg_connection *conn,
                      int status_code,
                      const char *message);

#endif /* RESPONSE_HELPER_H */
