/*
 * Student Controller Implementation
 * CRUD operations for the /api/students endpoint, backed by MongoDB.
 * Copyright (c) 2026
 * MIT License
 */

#include "student_controller.h"
#include "response_helper.h"
#include "../include/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared DB handle – defined in score_analyzer_backend.c */
extern db_connection_t *global_db;

/* ---------------------------------------------------------------------------
 * Internal helper: URL-decode a string in-place
 * --------------------------------------------------------------------------*/
static void url_decode_stu(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* ---------------------------------------------------------------------------
 * Internal helper: get a query-string parameter value
 * --------------------------------------------------------------------------*/
static int get_query_param_stu(const char *query_string, const char *param,
                                char *out, size_t out_size)
{
    if (!query_string || !*query_string) return 0;
    char search[256];
    snprintf(search, sizeof(search), "%s=", param);
    const char *p = strstr(query_string, search);
    if (!p) return 0;
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_size) len = out_size - 1;
    char encoded[512];
    strncpy(encoded, p, len);
    encoded[len] = '\0';
    url_decode_stu(encoded, out, out_size);
    return (out[0] != '\0') ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Internal helper: extract a quoted string value from a flat JSON object.
 * Caller must free() the returned string.
 * --------------------------------------------------------------------------*/
static char *extract_json_value(const char *json, const char *key)
{
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;

    key_pos += strlen(search_key);
    while (*key_pos == ' ' || *key_pos == '\t') key_pos++;   /* skip whitespace */
    if (*key_pos != '"') return NULL;                         /* expect quoted value */
    key_pos++;                                                /* skip opening quote */

    char *end_quote = strchr(key_pos, '"');
    if (!end_quote) return NULL;

    size_t value_len = (size_t)(end_quote - key_pos);
    char  *value     = malloc(value_len + 1);
    if (!value) return NULL;
    strncpy(value, key_pos, value_len);
    value[value_len] = '\0';
    return value;
}

/* ---------------------------------------------------------------------------
 * Internal helper: extract a numeric value from a flat JSON object.
 * Returns 1 on success, 0 on failure.
 * --------------------------------------------------------------------------*/
static int extract_json_number(const char *json, const char *key, double *out)
{
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char *key_pos = strstr(json, search_key);
    if (!key_pos) return 0;

    key_pos += strlen(search_key);
    while (*key_pos == ' ' || *key_pos == '\t') key_pos++;

    /* Could be a quoted number "85.5" or unquoted 85.5 */
    if (*key_pos == '"') {
        key_pos++;
        *out = atof(key_pos);
    } else {
        *out = atof(key_pos);
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 * GET /api/students          – list all students
 * GET /api/students/{id}     – get one student
 * GET /api/students/{id}/scores – get scores for a student
 * POST /api/students         – create student  { name, email, student_id }
 * POST /api/students/{id}/scores – add score    { subject, score }
 * PUT  /api/students/{id}    – update student  { name, email }
 * PUT  /api/students/{id}/scores – update score { subject, score }
 * DELETE /api/students/{id}  – delete student
 * DELETE /api/students/{id}/scores?subject=X – delete score
 * --------------------------------------------------------------------------*/
int UsersHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri  = mg_get_request_info(conn);
    const char                   *url = ri->local_uri;
    char userid[256] = "";
    int  want_scores = 0;

    printf("Students API: %s %s\n", ri->request_method, url);

    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    /* Extract student ID from URL if present: /api/students/{id}[/scores] */
    if (strncmp(url, "/api/students/", 14) == 0) {
        const char *id_start = url + 14;
        if (*id_start != '\0') {
            /* Check for /scores suffix */
            const char *slash = strchr(id_start, '/');
            if (slash && strcmp(slash, "/scores") == 0) {
                want_scores = 1;
                size_t id_len = (size_t)(slash - id_start);
                if (id_len >= sizeof(userid)) id_len = sizeof(userid) - 1;
                strncpy(userid, id_start, id_len);
                userid[id_len] = '\0';
            } else {
                strncpy(userid, id_start, sizeof(userid) - 1);
                userid[sizeof(userid) - 1] = '\0';
            }
        }
    }

    /* ---- GET ---- */
    if (0 == strcmp(ri->request_method, "GET")) {
        /* GET /api/students/{id}/scores */
        if (want_scores && strlen(userid) > 0) {
            char *scores_json = db_get_student_scores(global_db, userid);
            int rc = SendJSONResponse(conn, "success", "Scores retrieved successfully", scores_json);
            free(scores_json);
            return rc;
        }
        if (strlen(userid) > 0) {
            char *user_json = db_get_student_by_id(global_db, userid);
            if (user_json && strcmp(user_json, "null") != 0) {
                int rc = SendJSONResponse(conn, "success", "Student retrieved successfully", user_json);
                free(user_json);
                return rc;
            }
            if (user_json) free(user_json);
            return SendErrorResponse(conn, 404, "Student not found");
        }
        /* Check for ?class= filter */
        char class_filter[256] = "";
        get_query_param_stu(ri->query_string, "class", class_filter, sizeof(class_filter));

        char *users_json;
        if (class_filter[0] != '\0')
            users_json = db_get_students_by_class(global_db, class_filter);
        else
            users_json = db_get_all_students(global_db);

        int   rc = SendJSONResponse(conn, "success", "Students retrieved successfully", users_json);
        free(users_json);
        return rc;
    }

    /* ---- POST ---- */
    if (0 == strcmp(ri->request_method, "POST")) {
        char buffer[1024];
        int  dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");

        buffer[dlen] = '\0';
        printf("Received POST data: %s\n", buffer);

        /* POST /api/students/{id}/scores  { subject, score } */
        if (want_scores && strlen(userid) > 0) {
            char   *subject = extract_json_value(buffer, "subject");
            double  score_val = 0;
            int     has_score = extract_json_number(buffer, "score", &score_val);

            if (!subject || !has_score) {
                free(subject);
                return SendErrorResponse(conn, 400,
                    "Missing required fields: subject, score");
            }

            int ok = db_add_score(global_db, userid, subject, score_val);
            free(subject);
            if (ok) {
                char *scores = db_get_student_scores(global_db, userid);
                int rc = SendJSONResponse(conn, "success", "Score added successfully", scores);
                free(scores);
                return rc;
            }
            return SendErrorResponse(conn, 500, "Failed to add score");
        }

        char *name       = extract_json_value(buffer, "name");
        char *email      = extract_json_value(buffer, "email");
        char *student_id = extract_json_value(buffer, "student_id");
        char *class_name = extract_json_value(buffer, "class_name");

        if (!name || !email || !student_id) {
            free(name); free(email); free(student_id); free(class_name);
            return SendErrorResponse(conn, 400,
                "Missing required fields: name, email, student_id");
        }

        int ok;
        if (class_name && class_name[0] != '\0')
            ok = db_create_student_with_class(global_db, name, email, student_id, class_name);
        else
            ok = db_create_student(global_db, name, email, student_id);

        if (ok) {
            char *created = db_get_student_by_id(global_db, student_id);
            int   rc      = SendJSONResponse(conn, "success", "Student created successfully", created);
            free(created); free(name); free(email); free(student_id); free(class_name);
            return rc;
        }
        free(name); free(email); free(student_id); free(class_name);
        return SendErrorResponse(conn, 500, "Failed to create student in database");
    }

    /* ---- PUT ---- */
    if (0 == strcmp(ri->request_method, "PUT")) {
        if (strlen(userid) == 0)
            return SendErrorResponse(conn, 400, "Student ID required for PUT request");

        char buffer[1024];
        int  dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");

        buffer[dlen] = '\0';

        /* PUT /api/students/{id}/scores  { subject, score } */
        if (want_scores) {
            printf("Received PUT score data for student %s: %s\n", userid, buffer);

            char   *subject = extract_json_value(buffer, "subject");
            double  score_val = 0;
            int     has_score = extract_json_number(buffer, "score", &score_val);

            if (!subject || !has_score) {
                free(subject);
                return SendErrorResponse(conn, 400,
                    "Missing required fields: subject, score");
            }

            int ok = db_update_score(global_db, userid, subject, score_val);
            free(subject);
            if (ok) {
                char *scores = db_get_student_scores(global_db, userid);
                int rc = SendJSONResponse(conn, "success", "Score updated successfully", scores);
                free(scores);
                return rc;
            }
            return SendErrorResponse(conn, 500, "Failed to update score");
        }

        printf("Received PUT data for student %s: %s\n", userid, buffer);

        char *name  = extract_json_value(buffer, "name");
        char *email = extract_json_value(buffer, "email");

        if (!name || !email) {
            free(name); free(email);
            return SendErrorResponse(conn, 400, "Missing required fields: name, email");
        }

        int ok = db_update_student(global_db, userid, name, email);
        if (ok) {
            char *updated = db_get_student_by_id(global_db, userid);
            int   rc      = SendJSONResponse(conn, "success", "Student updated successfully", updated);
            free(updated); free(name); free(email);
            return rc;
        }
        free(name); free(email);
        return SendErrorResponse(conn, 500, "Failed to update student in database");
    }

    /* ---- DELETE ---- */
    if (0 == strcmp(ri->request_method, "DELETE")) {
        if (strlen(userid) == 0)
            return SendErrorResponse(conn, 400, "Student ID required for DELETE request");

        /* DELETE /api/students/{id}/scores?subject=X */
        if (want_scores) {
            char subject_filter[256] = "";
            get_query_param_stu(ri->query_string, "subject", subject_filter, sizeof(subject_filter));
            if (subject_filter[0] == '\0')
                return SendErrorResponse(conn, 400, "Query parameter 'subject' is required to delete a score");

            printf("DELETE score for student %s, subject %s\n", userid, subject_filter);

            int ok = db_delete_score(global_db, userid, subject_filter);
            if (ok) {
                char *scores = db_get_student_scores(global_db, userid);
                int rc = SendJSONResponse(conn, "success", "Score deleted successfully", scores);
                free(scores);
                return rc;
            }
            return SendErrorResponse(conn, 500, "Failed to delete score");
        }

        printf("DELETE student %s\n", userid);

        int ok = db_delete_student(global_db, userid);
        if (ok) {
            mg_printf(conn,
                      "HTTP/1.1 204 No Content\r\n"
                      "Connection: close\r\n\r\n");
            return 204;
        }
        return SendErrorResponse(conn, 500, "Failed to delete student from database");
    }

    return SendErrorResponse(conn, 405, "Method not supported for students endpoint");
}
