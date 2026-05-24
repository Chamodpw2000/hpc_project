/*
 * HTTP Response Helper
 * Shared JSON and error response utilities for all controllers.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef RESPONSE_HELPER_H
#define RESPONSE_HELPER_H

#include "../include/civetweb.h"

/* Shared global request counter */
extern unsigned requestCounter;

/* Send a standard JSON success response (status, message, raw data) */
int SendJSONResponse(struct mg_connection *conn,
                     const char *status,
                     const char *message,
                     const char *data);

/* Send a JSON error response with status code */
int SendErrorResponse(struct mg_connection *conn,
                      int status_code,
                      const char *message);

#endif /* RESPONSE_HELPER_H */
