/*
 * Class & Subject Controller
 * Handles /api/classes and /api/subjects endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CLASS_CONTROLLER_H
#define CLASS_CONTROLLER_H

#include "../include/civetweb.h"

/*
 * GET    /api/classes        – list all classes (JSON array of strings)
 * POST   /api/classes        – create class  { "name": "..." }
 * DELETE /api/classes/{name} – delete a class
 */
int ClassHandler(struct mg_connection *conn, void *cbdata);

/*
 * GET    /api/subjects                    – list all subjects
 * GET    /api/subjects?class={name}       – list subjects for a class
 * POST   /api/subjects                    – create subject { "name": "...", "class_name": "..." }
 * DELETE /api/subjects/{name}?class={cn}  – delete a subject
 */
int SubjectHandler(struct mg_connection *conn, void *cbdata);

#endif /* CLASS_CONTROLLER_H */
