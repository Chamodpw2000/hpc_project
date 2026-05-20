/*
 * Correlation Controller
 * Handles GET /api/calculate/correlation/serial|parallel|compare
 *
 * Required query parameters:
 *   subject1, class1  – first subject + its class
 *   subject2, class2  – second subject + its class
 *
 * Example:
 *   GET /api/calculate/correlation/serial?subject1=Math&class1=A&subject2=Science&class2=A
 *
 * Copyright (c) 2026
 * MIT License
 */

#include "correlation_controller.h"
#include "correlation_engine.h"
#include "response_helper.h"
#include "../include/db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#ifdef ENABLE_MPI
#include <mpi.h>
#include "correlation_mpi.h"
#include "calc_mpi.h"
#endif

extern db_connection_t *global_db;

/* ── Parse a single query parameter (URL-decoded by mg_get_var) ── */
static int get_param(const char *qs, size_t qs_len,
                     const char *name, char *out, size_t out_sz)
{
    int ret = mg_get_var(qs, qs_len, name, out, out_sz);
    return (ret > 0) ? 1 : 0;
}

/* ── Shared: parse params + fetch pairs ── */
static score_pair_t* prepare_pairs(struct mg_connection *conn,
                                    int *out_n, double *out_fetch_ms,
                                    char *sub1, char *cls1,
                                    char *sub2, char *cls2)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *qs  = ri->query_string ? ri->query_string : "";
    size_t      qsl = strlen(qs);

    if (!get_param(qs, qsl, "subject1", sub1, 256) ||
        !get_param(qs, qsl, "class1",   cls1, 256) ||
        !get_param(qs, qsl, "subject2", sub2, 256) ||
        !get_param(qs, qsl, "class2",   cls2, 256)) {
        SendErrorResponse(conn, 400,
            "Missing required parameters: subject1, class1, subject2, class2");
        return NULL;
    }

    score_pair_t *pairs = NULL;
    int n = 0;
    db_get_paired_scores(global_db, sub1, cls1, sub2, cls2,
                         &pairs, &n, out_fetch_ms);

    if (!pairs || n < 2) {
        if (pairs) free(pairs);
        SendErrorResponse(conn, 404,
            "Not enough paired scores found. "
            "Ensure both subjects exist in the specified classes "
            "and students have scores for both.");
        return NULL;
    }

    *out_n = n;
    return pairs;
}

/* ── GET /api/calculate/correlation/serial ── */
int CorrSerialHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char sub1[256], cls1[256], sub2[256], cls2[256];
    double fetch_ms = 0.0;
    int n = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    corr_result_t r = run_corr_serial(pairs, n);
    omp_set_num_threads(prev);

    char *data = format_corr_json(&r, "serial", fetch_ms, pairs, n);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "Serial correlation calculation completed", data);
    free(data);
    return ret;
}

/* ── GET /api/calculate/correlation/parallel ── */
int CorrParallelHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char sub1[256], cls1[256], sub2[256], cls2[256];
    double fetch_ms = 0.0;
    int n = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    corr_result_t r = run_corr_parallel(pairs, n);

    char *data = format_corr_json(&r, "parallel", fetch_ms, pairs, n);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "Parallel (OpenMP) correlation calculation completed", data);
    free(data);
    return ret;
}

/* ── GET /api/calculate/correlation/compare ── */
int CorrCompareHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char sub1[256], cls1[256], sub2[256], cls2[256];
    double fetch_ms = 0.0;
    int n = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    /* Run serial first (force single thread) */
    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    corr_result_t serial = run_corr_serial(pairs, n);
    omp_set_num_threads(prev);

    /* Run parallel on same data */
    corr_result_t parallel = run_corr_parallel(pairs, n);

#ifdef ENABLE_MPI
#include <mpi.h>
#include "correlation_mpi.h"
#include "calc_mpi.h"
    /* Signal workers to participate in correlation calculation */
    int cmd = MPI_CMD_CALC_CORR;
    MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);
    corr_result_t mpi_res = run_corr_mpi(pairs, n);
    double mpi_time_ms = mpi_res.elapsed_ms;
    double speedup_mpi = (mpi_time_ms > 0) ? serial.elapsed_ms / mpi_time_ms : 0.0;
    int mpi_threads = mpi_res.threads_used;
#else
    double mpi_time_ms = 0.0;
    double speedup_mpi = 0.0;
    int mpi_threads = 0;
#endif

    /* Format individual result JSON (include data_points in both panels) */
    char *ser_json = format_corr_json(&serial,   "serial",   fetch_ms, pairs, n);
    char *par_json = format_corr_json(&parallel, "parallel", 0.0,      pairs, n);
#ifdef ENABLE_MPI
    char *mpi_json = format_corr_json(&mpi_res,  "mpi",      0.0,      pairs, n);
#else
    char mpi_json_str[] = "null";
    char *mpi_json = strdup(mpi_json_str);
#endif
    free(pairs);

    if (!ser_json || !par_json || !mpi_json) {
        if (ser_json) free(ser_json);
        if (par_json) free(par_json);
        if (mpi_json) free(mpi_json);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }

    double speedup = (parallel.elapsed_ms > 0.0)
        ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;
    double improvement_pct = (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0;

    /* Build combined response (+1024 for comparison block to be safe) */
    size_t data_sz = strlen(ser_json) + strlen(par_json) + strlen(mpi_json) + 1024;
    char  *data    = (char *)malloc(data_sz);
    if (!data) {
        free(ser_json); free(par_json); free(mpi_json);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }

    snprintf(data, data_sz,
        "{\n"
        "    \"serial\": %s,\n"
        "    \"parallel\": %s,\n"
        "    \"mpi\": %s,\n"
        "    \"comparison\": {\n"
        "      \"serial_time_ms\": %.4f,\n"
        "      \"parallel_time_ms\": %.4f,\n"
        "      \"mpi_time_ms\": %.4f,\n"
        "      \"db_fetch_ms\": %.4f,\n"
        "      \"speedup\": %.4f,\n"
        "      \"speedup_mpi\": %.4f,\n"
        "      \"serial_threads\": %d,\n"
        "      \"parallel_threads\": %d,\n"
        "      \"mpi_threads\": %d,\n"
        "      \"n_pairs\": %d,\n"
        "      \"improvement_pct\": %.2f\n"
        "    }\n"
        "  }",
        ser_json, par_json, mpi_json,
        serial.elapsed_ms, parallel.elapsed_ms, mpi_time_ms, fetch_ms,
        speedup, speedup_mpi,
        serial.threads_used, parallel.threads_used, mpi_threads,
        n,
        improvement_pct);

    free(ser_json);
    free(par_json);
    free(mpi_json);

    int ret = SendJSONResponse(conn, "success",
        "Serial vs Parallel vs MPI correlation comparison completed", data);
    free(data);
    return ret;
}

/* ── MPI distributed correlation ───────────────────────────────────────── */
#ifdef ENABLE_MPI
#include "correlation_mpi.h"
#include "calc_mpi.h"   /* MPI_CMD_* */
#include <mpi.h>

int CorrMpiHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char sub1[256], cls1[256], sub2[256], cls2[256];
    double fetch_ms = 0.0;
    int n = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    /* Signal workers to participate in correlation calculation */
    int cmd = MPI_CMD_CALC_CORR;
    MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);

    corr_result_t r = run_corr_mpi(pairs, n);

    char *data = format_corr_json(&r, "mpi", fetch_ms, pairs, n);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "MPI distributed correlation calculation completed", data);
    free(data);
    return ret;
}
#endif /* ENABLE_MPI */
