/*
 * Class & Subject Controller Implementation
 * CRUD for /api/classes and /api/subjects backed by MongoDB.
 * Copyright (c) 2026
 * MIT License
 */

#include "class_controller.h"
#include "response_helper.h"
#include "../include/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared DB handle – defined in score_analyzer_backend.c */
extern db_connection_t *global_db;

/* ── internal helper: extract a quoted JSON string value ── */
static char *extract_json_str(const char *json, const char *key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    char *end = strchr(p, '"');
    if (!end) return NULL;
    size_t len = (size_t)(end - p);
    char  *val = malloc(len + 1);
    if (!val) return NULL;
    strncpy(val, p, len);
    val[len] = '\0';
    return val;
}

/* ── internal helper: URL-decode a single %XX sequence in-place ── */
static void url_decode(const char *src, char *dst, size_t dst_size)
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

/* ── helper: get query param value from URI ── */
static int get_query_param(const char *query_string, const char *param,
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
    url_decode(encoded, out, out_size);
    return (out[0] != '\0') ? 1 : 0;
}

/* ==========================================================================
 * GET    /api/classes
 * POST   /api/classes        body: { "name": "..." }
 * DELETE /api/classes/{name}
 * =========================================================================*/
int ClassHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri  = mg_get_request_info(conn);
    const char                   *url = ri->local_uri;

    printf("Classes API: %s %s\n", ri->request_method, url);

    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    /* Extract optional name from URL: /api/classes/{name} */
    char class_name[256] = "";
    if (strncmp(url, "/api/classes/", 13) == 0) {
        url_decode(url + 13, class_name, sizeof(class_name));
    }

    /* ── GET: list all classes ── */
    if (strcmp(ri->request_method, "GET") == 0) {
        char *list = db_get_all_classes(global_db);
        int rc = SendJSONResponse(conn, "success", "Classes retrieved successfully", list);
        free(list);
        return rc;
    }

    /* ── POST: create class ── */
    if (strcmp(ri->request_method, "POST") == 0) {
        char buf[512];
        int  dlen = mg_read(conn, buf, sizeof(buf) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");
        buf[dlen] = '\0';

        char *name = extract_json_str(buf, "name");
        if (!name || name[0] == '\0') {
            free(name);
            return SendErrorResponse(conn, 400, "Missing required field: name");
        }

        if (!db_create_class(global_db, name)) {
            free(name);
            return SendErrorResponse(conn, 500, "Failed to create class");
        }

        char resp[256];
        snprintf(resp, sizeof(resp), "{\"name\":\"%s\"}", name);
        free(name);
        return SendJSONResponse(conn, "success", "Class created successfully", resp);
    }

    /* ── DELETE: /api/classes/{name} ── */
    if (strcmp(ri->request_method, "DELETE") == 0) {
        if (class_name[0] == '\0')
            return SendErrorResponse(conn, 400, "Class name required in URL");

        if (!db_delete_class(global_db, class_name))
            return SendErrorResponse(conn, 500, "Failed to delete class");

        mg_printf(conn, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
        return 204;
    }

    /* ── PUT: /api/classes/{name}  body: { "name": "new_name" } ── */
    if (strcmp(ri->request_method, "PUT") == 0) {
        if (class_name[0] == '\0')
            return SendErrorResponse(conn, 400, "Class name required in URL");

        char buf[512];
        int  dlen = mg_read(conn, buf, sizeof(buf) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");
        buf[dlen] = '\0';

        char *new_name = extract_json_str(buf, "name");
        if (!new_name || new_name[0] == '\0') {
            free(new_name);
            return SendErrorResponse(conn, 400, "Missing required field: name");
        }

        if (!db_rename_class(global_db, class_name, new_name)) {
            free(new_name);
            return SendErrorResponse(conn, 500, "Failed to rename class");
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"old_name\":\"%s\",\"name\":\"%s\"}", class_name, new_name);
        free(new_name);
        return SendJSONResponse(conn, "success", "Class renamed successfully", resp);
    }

    return SendErrorResponse(conn, 405, "Method not supported for /api/classes");
}

/* ==========================================================================
 * GET    /api/subjects                    – all subjects
 * GET    /api/subjects?class={name}       – subjects for a class
 * POST   /api/subjects                    body: { "name": "...", "class_name": "..." }
 * DELETE /api/subjects/{name}?class={cn}
 * =========================================================================*/
int SubjectHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri  = mg_get_request_info(conn);
    const char                   *url = ri->local_uri;

    printf("Subjects API: %s %s\n", ri->request_method, url);

    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    /* Extract optional subject name from URL: /api/subjects/{name} */
    char subj_name[256] = "";
    if (strncmp(url, "/api/subjects/", 14) == 0) {
        url_decode(url + 14, subj_name, sizeof(subj_name));
    }

    /* ── GET ── */
    if (strcmp(ri->request_method, "GET") == 0) {
        char class_filter[256] = "";
        get_query_param(ri->query_string, "class", class_filter, sizeof(class_filter));

        char *list;
        if (class_filter[0] != '\0')
            list = db_get_subjects_by_class(global_db, class_filter);
        else
            list = db_get_all_subjects(global_db);

        int rc = SendJSONResponse(conn, "success", "Subjects retrieved successfully", list);
        free(list);
        return rc;
    }

    /* ── POST: create subject ── */
    if (strcmp(ri->request_method, "POST") == 0) {
        char buf[512];
        int  dlen = mg_read(conn, buf, sizeof(buf) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");
        buf[dlen] = '\0';

        char *name  = extract_json_str(buf, "name");
        char *cname = extract_json_str(buf, "class_name");

        if (!name || !cname || name[0] == '\0' || cname[0] == '\0') {
            free(name); free(cname);
            return SendErrorResponse(conn, 400, "Missing required fields: name, class_name");
        }

        if (!db_create_subject(global_db, name, cname)) {
            free(name); free(cname);
            return SendErrorResponse(conn, 500, "Failed to create subject");
        }

        char resp[512];
        snprintf(resp, sizeof(resp), "{\"name\":\"%s\",\"class_name\":\"%s\"}", name, cname);
        free(name); free(cname);
        return SendJSONResponse(conn, "success", "Subject created successfully", resp);
    }

    /* ── DELETE: /api/subjects/{name}?class={cn} ── */
    if (strcmp(ri->request_method, "DELETE") == 0) {
        char cname[256] = "";
        get_query_param(ri->query_string, "class", cname, sizeof(cname));

        if (subj_name[0] == '\0' || cname[0] == '\0')
            return SendErrorResponse(conn, 400,
                "Subject name in URL and class query param required");

        if (!db_delete_subject(global_db, subj_name, cname))
            return SendErrorResponse(conn, 500, "Failed to delete subject");

        mg_printf(conn, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
        return 204;
    }

    /* ── PUT: /api/subjects/{name}?class={cn}  body: { "name": "new_name" } ── */
    if (strcmp(ri->request_method, "PUT") == 0) {
        char cname[256] = "";
        get_query_param(ri->query_string, "class", cname, sizeof(cname));

        if (subj_name[0] == '\0' || cname[0] == '\0')
            return SendErrorResponse(conn, 400,
                "Subject name in URL and class query param required");

        char buf[512];
        int  dlen = mg_read(conn, buf, sizeof(buf) - 1);
        if (dlen <= 0)
            return SendErrorResponse(conn, 400, "No data received");
        buf[dlen] = '\0';

        char *new_name = extract_json_str(buf, "name");
        if (!new_name || new_name[0] == '\0') {
            free(new_name);
            return SendErrorResponse(conn, 400, "Missing required field: name");
        }

        if (!db_rename_subject(global_db, subj_name, cname, new_name)) {
            free(new_name);
            return SendErrorResponse(conn, 500, "Failed to rename subject");
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"old_name\":\"%s\",\"name\":\"%s\",\"class_name\":\"%s\"}",
                 subj_name, new_name, cname);
        free(new_name);
        return SendJSONResponse(conn, "success", "Subject renamed successfully", resp);
    }

    return SendErrorResponse(conn, 405, "Method not supported for /api/subjects");
}
