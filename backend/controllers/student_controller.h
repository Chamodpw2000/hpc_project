/*
 * Student Controller
 * Handles CRUD operations for /api/students endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef STUDENT_CONTROLLER_H
#define STUDENT_CONTROLLER_H

#include "../include/civetweb.h"

/*
 * Handles all /api/students and /api/students/{id} requests.
 * Supported methods: GET, POST, PUT, DELETE
 */
int UsersHandler(struct mg_connection *conn, void *cbdata);

#endif /* STUDENT_CONTROLLER_H */
