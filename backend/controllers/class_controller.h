/*
 * Class & Subject Controller
 * Handles /api/classes and /api/subjects endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CLASS_CONTROLLER_H
#define CLASS_CONTROLLER_H

#include "../include/civetweb.h"

/* GET /api/classes - list classes; POST /api/classes - create class; DELETE /api/classes/{name} - delete class */
int ClassHandler(struct mg_connection *conn, void *cbdata);

/* GET /api/subjects - list subjects; POST /api/subjects - create subject; DELETE /api/subjects/{name} - delete subject */
int SubjectHandler(struct mg_connection *conn, void *cbdata);

#endif /* CLASS_CONTROLLER_H */
