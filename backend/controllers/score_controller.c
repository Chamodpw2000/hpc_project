/*
 * Score Controller Implementation
 * Seed endpoint + Serial / Parallel / Compare calculation endpoints.
 * Copyright (c) 2026
 * MIT License
 */

#include "score_controller.h"
#include "response_helper.h"
#include "calc_engine.h"
#include "../include/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

/* Shared DB handle – defined in score_analyzer_backend.c */
extern db_connection_t *global_db;

/* ---- POST /api/seed ------------------------------------------------------- */
int SeedHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "POST") != 0)
        return SendErrorResponse(conn, 405,
            "Only POST method supported. "
            "Send POST with optional JSON {num_students}");

    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    /* Defaults */
    int num_students = 100;

    /* Parse optional request body */
    char buffer[512];
    int  dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
    if (dlen > 0) {
        buffer[dlen] = '\0';
        char *p;
        if ((p = strstr(buffer, "\"num_students\"")) != NULL) {
            p = strchr(p, ':');
            if (p) num_students = atoi(p + 1);
        }
    }
    if (num_students < 1) num_students = 100;

    int total = db_seed_dummy_data(global_db, num_students, 0);

    char data[512];
    snprintf(data, sizeof(data),
        "{\n"
        "    \"students_created\": %d,\n"
        "    \"scores_created\": %d\n"
        "  }",
        num_students, total);

    return SendJSONResponse(conn, "success", "Dummy data seeded successfully", data);
}

/* ---- GET /api/calculate/serial ------------------------------------------- */
int CalcSerialHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int     count  = 0;
    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    /* Force single thread for the serial run */
    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    calc_result_t r = run_serial(scores, count);
    omp_set_num_threads(prev);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "serial", db_fetch_ms);
    return SendJSONResponse(conn, "success", "Serial calculation completed", data);
}

/* ---- GET /api/calculate/parallel ----------------------------------------- */
int CalcParallelHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int     count  = 0;
    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    calc_result_t r = run_parallel(scores, count);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "parallel", db_fetch_ms);
    return SendJSONResponse(conn, "success",
        "Parallel (OpenMP) calculation completed", data);
}

/* ---- GET /api/calculate/compare ------------------------------------------ */
int CalcCompareHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int     count  = 0;
    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    /* Serial run (force 1 thread) */
    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    calc_result_t serial = run_serial(scores, count);
    omp_set_num_threads(prev);

    /* Parallel run */
    calc_result_t parallel = run_parallel(scores, count);
    free(scores);

    double speedup = (parallel.elapsed_ms > 0)
        ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;

    char ser_json[2048], par_json[2048];
    format_result_json(ser_json, sizeof(ser_json), &serial,   "serial",   db_fetch_ms);
    format_result_json(par_json, sizeof(par_json), &parallel, "parallel", db_fetch_ms);

    char *data = (char *)malloc(8192);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    snprintf(data, 8192,
        "{\n"
        "    \"serial\": %s,\n"
        "    \"parallel\": %s,\n"
        "    \"comparison\": {\n"
        "      \"serial_time_ms\": %.4f,\n"
        "      \"parallel_time_ms\": %.4f,\n"
        "      \"db_fetch_ms\": %.4f,\n"
        "      \"speedup\": %.4f,\n"
        "      \"serial_threads\": %d,\n"
        "      \"parallel_threads\": %d,\n"
        "      \"data_size\": %d,\n"
        "      \"improvement_pct\": %.2f\n"
        "    }\n"
        "  }",
        ser_json, par_json,
        serial.elapsed_ms, parallel.elapsed_ms, db_fetch_ms, speedup,
        serial.threads_used, parallel.threads_used,
        count,
        (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0);

    int result = SendJSONResponse(conn, "success",
        "Serial vs Parallel comparison completed", data);
    free(data);
    return result;
}
