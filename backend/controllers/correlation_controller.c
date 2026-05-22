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
#include <time.h>
#include <errno.h>

#include "../include/calc_lock.h"

#ifdef ENABLE_MPI
#include <mpi.h>
#include "correlation_mpi.h"
#include "calc_mpi.h"
#endif

extern db_connection_t *global_db;
extern int g_num_threads;

static int cc_buf_append(char **buf, size_t *len, size_t *cap, const char *src)
{
    size_t slen = strlen(src);
    if (*len + slen + 1 > *cap) {
        *cap = (*len + slen + 1) * 2;
        char *tmp = (char *)realloc(*buf, *cap);
        if (!tmp) return 0;
        *buf = tmp;
    }
    strcpy(*buf + *len, src);
    *len += slen;
    return 1;
}

/* ── Parse a single query parameter (URL-decoded by mg_get_var) ── */
static int get_param(const char *qs, size_t qs_len,
                     const char *name, char *out, size_t out_sz)
{
    int ret = mg_get_var(qs, qs_len, name, out, out_sz);
    return (ret > 0) ? 1 : 0;
}

static int get_thread_count(const struct mg_request_info *ri) {
    if (!ri->query_string) return 0;
    char buf[16] = "";
    mg_get_var(ri->query_string, strlen(ri->query_string), "threads", buf, sizeof(buf));
    int t = atoi(buf);
    return (t > 0 && t <= 256) ? t : 0;
}

/* ── Shared: parse params + fetch pairs ── */
static score_pair_t* prepare_pairs(struct mg_connection *conn,
                                    int *out_n, double *out_fetch_ms,
                                    int *out_total_students, int *out_excluded,
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
                         &pairs, &n, out_total_students, out_excluded, out_fetch_ms);

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
    int n = 0, total_students = 0, excluded = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        &total_students, &excluded,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        free(pairs);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    corr_result_t r = run_corr_serial(pairs, n);
    omp_set_num_threads(prev);
    
    pthread_mutex_unlock(&calc_lock);

    char *data = format_corr_json(&r, "serial", fetch_ms, pairs, n, total_students, excluded);
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
    int n = 0, total_students = 0, excluded = 0;
    int req_threads = get_thread_count(ri);

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        &total_students, &excluded,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        free(pairs);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int prev = omp_get_max_threads();
    if (req_threads > 0) omp_set_num_threads(req_threads);
    corr_result_t r = run_corr_parallel(pairs, n);
    omp_set_num_threads(prev);

    pthread_mutex_unlock(&calc_lock);

    char *data = format_corr_json(&r, "parallel", fetch_ms, pairs, n, total_students, excluded);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "Parallel (OpenMP) correlation calculation completed", data);
    free(data);
    return ret;
}

/* ── GET /api/calculate/correlation/pthread ── */
int CorrPthreadHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char sub1[256], cls1[256], sub2[256], cls2[256];
    double fetch_ms = 0.0;
    int n = 0, total_students = 0, excluded = 0;
    int req_threads = get_thread_count(ri);

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        &total_students, &excluded,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        free(pairs);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int saved_threads = g_num_threads;
    if (req_threads > 0) g_num_threads = req_threads;
    corr_result_t r = run_corr_pthread(pairs, n);
    g_num_threads = saved_threads;

    pthread_mutex_unlock(&calc_lock);

    char *data = format_corr_json(&r, "pthread", fetch_ms, pairs, n, total_students, excluded);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "Pthreads correlation calculation completed", data);
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
    int n = 0, total_students = 0, excluded = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        &total_students, &excluded,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        free(pairs);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int req_threads = get_thread_count(ri);

    /* Run serial first (force single thread) */
    int prev = omp_get_max_threads();
    int saved_threads = g_num_threads;
    omp_set_num_threads(1);
    corr_result_t serial = run_corr_serial(pairs, n);

    if (req_threads > 0) {
        omp_set_num_threads(req_threads);
        g_num_threads = req_threads;
    } else {
        omp_set_num_threads(prev);
    }

    /* Run parallel on same data */
    corr_result_t parallel = run_corr_parallel(pairs, n);

    /* Run pthreads on same data */
    corr_result_t pt_res = run_corr_pthread(pairs, n);
    omp_set_num_threads(prev);
    g_num_threads = saved_threads;

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

    pthread_mutex_unlock(&calc_lock);

    /* Format individual result JSON (include data_points in both panels) */
    char *ser_json = format_corr_json(&serial,   "serial",   fetch_ms, pairs, n, total_students, excluded);
    char *par_json = format_corr_json(&parallel, "parallel", 0.0,      pairs, n, total_students, excluded);
    char *pt_json  = format_corr_json(&pt_res,   "pthread",  0.0,      pairs, n, total_students, excluded);
#ifdef ENABLE_MPI
    char *mpi_json = format_corr_json(&mpi_res,  "mpi",      0.0,      pairs, n, total_students, excluded);
#else
    char mpi_json_str[] = "null";
    char *mpi_json = strdup(mpi_json_str);
#endif
    free(pairs);

    if (!ser_json || !par_json || !pt_json || !mpi_json) {
        if (ser_json) free(ser_json);
        if (par_json) free(par_json);
        if (pt_json)  free(pt_json);
        if (mpi_json) free(mpi_json);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }

    double speedup = (parallel.elapsed_ms > 0.0)
        ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;
    double speedup_pt = (pt_res.elapsed_ms > 0.0)
        ? serial.elapsed_ms / pt_res.elapsed_ms : 0.0;
    double improvement_pct = (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0;

    /* Build combined response */
    size_t data_sz = strlen(ser_json) + strlen(par_json) + strlen(pt_json) + strlen(mpi_json) + 2048;
    char  *data    = (char *)malloc(data_sz);
    if (!data) {
        free(ser_json); free(par_json); free(pt_json); free(mpi_json);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }

    snprintf(data, data_sz,
        "{\n"
        "    \"serial\": %s,\n"
        "    \"parallel\": %s,\n"
        "    \"pthread\": %s,\n"
        "    \"mpi\": %s,\n"
        "    \"comparison\": {\n"
        "      \"serial_time_ms\": %.4f,\n"
        "      \"parallel_time_ms\": %.4f,\n"
        "      \"pthread_time_ms\": %.4f,\n"
        "      \"mpi_time_ms\": %.4f,\n"
        "      \"db_fetch_ms\": %.4f,\n"
        "      \"speedup\": %.4f,\n"
        "      \"speedup_pthread\": %.4f,\n"
        "      \"speedup_mpi\": %.4f,\n"
        "      \"serial_threads\": %d,\n"
        "      \"parallel_threads\": %d,\n"
        "      \"pthread_threads\": %d,\n"
        "      \"mpi_threads\": %d,\n"
        "      \"n_pairs\": %d,\n"
        "      \"total_students\": %d,\n"
        "      \"excluded\": %d,\n"
        "      \"improvement_pct\": %.2f\n"
        "    }\n"
        "  }",
        ser_json, par_json, pt_json, mpi_json,
        serial.elapsed_ms, parallel.elapsed_ms, pt_res.elapsed_ms, mpi_time_ms, fetch_ms,
        speedup, speedup_pt, speedup_mpi,
        serial.threads_used, parallel.threads_used, pt_res.threads_used, mpi_threads,
        n,
        total_students,
        excluded,
        improvement_pct);

    free(ser_json);
    free(par_json);
    free(pt_json);
    free(mpi_json);

    int ret = SendJSONResponse(conn, "success",
        "Serial vs Parallel vs Pthread vs MPI correlation comparison completed", data);
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
    int n = 0, total_students = 0, excluded = 0;

    score_pair_t *pairs = prepare_pairs(conn, &n, &fetch_ms,
                                        &total_students, &excluded,
                                        sub1, cls1, sub2, cls2);
    if (!pairs) return 1;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        free(pairs);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    /* Signal workers to participate in correlation calculation */
    int cmd = MPI_CMD_CALC_CORR;
    MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);

    corr_result_t r = run_corr_mpi(pairs, n);
    
    pthread_mutex_unlock(&calc_lock);

    char *data = format_corr_json(&r, "mpi", fetch_ms, pairs, n, total_students, excluded);
    free(pairs);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    int ret = SendJSONResponse(conn, "success",
        "MPI distributed correlation calculation completed", data);
    free(data);
    return ret;
}
#endif /* ENABLE_MPI */

/* ── GET /api/calculate/correlation/all-subjects ──────────────────────────
 * Correlates one reference subject against every other subject in the class.
 * Query params: class, subject (reference), threads (optional)
 * ──────────────────────────────────────────────────────────────────────── */
int CorrAllSubjectsHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);
    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    const char *qs  = ri->query_string ? ri->query_string : "";
    size_t      qsl = strlen(qs);
    char class_name[256] = "", ref_subject[256] = "";
    mg_get_var(qs, qsl, "class",   class_name,   sizeof(class_name));
    mg_get_var(qs, qsl, "subject", ref_subject,  sizeof(ref_subject));
    if (!class_name[0] || !ref_subject[0])
        return SendErrorResponse(conn, 400, "Missing required parameters: class, subject");

    int req_threads = get_thread_count(ri);

    char **subjects = NULL;
    int subject_count = db_get_class_subject_names(global_db, class_name, &subjects);
    if (subject_count <= 0) {
        if (subjects) free(subjects);
        return SendErrorResponse(conn, 404, "No subjects found for this class");
    }

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 60;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        for (int i = 0; i < subject_count; i++) free(subjects[i]);
        free(subjects);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int prev     = omp_get_max_threads();
    int saved_g  = g_num_threads;
    int tval     = (req_threads > 0) ? req_threads : prev;
    if (req_threads > 0) { omp_set_num_threads(req_threads); g_num_threads = req_threads; }

    double total_serial_ms = 0, total_par_ms = 0, total_pt_ms = 0;
    double total_mpi_ms = 0, total_db_ms = 0;
    int processed = 0;

    size_t subj_cap = 65536, subj_len = 0;
    char  *subj_buf = (char *)malloc(subj_cap);
    if (!subj_buf) {
        omp_set_num_threads(prev); g_num_threads = saved_g;
        pthread_mutex_unlock(&calc_lock);
        for (int i = 0; i < subject_count; i++) free(subjects[i]);
        free(subjects);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }
    subj_buf[0] = '\0';

    for (int i = 0; i < subject_count; i++) {
        if (strcmp(subjects[i], ref_subject) == 0) continue;

        score_pair_t *pairs = NULL;
        int n = 0, total_students = 0, excluded = 0;
        double db_ms = 0.0;
        db_get_paired_scores(global_db, ref_subject, class_name,
                             subjects[i], class_name,
                             &pairs, &n, &total_students, &excluded, &db_ms);
        total_db_ms += db_ms;
        if (!pairs || n < 2) { if (pairs) free(pairs); continue; }

        /* Serial — force 1 thread */
        omp_set_num_threads(1); g_num_threads = 1;
        corr_result_t serial = run_corr_serial(pairs, n);

        /* Restore threads for parallel methods */
        omp_set_num_threads(tval); g_num_threads = tval;
        corr_result_t par = run_corr_parallel(pairs, n);
        corr_result_t pt  = run_corr_pthread(pairs, n);

        total_serial_ms += serial.elapsed_ms;
        total_par_ms    += par.elapsed_ms;
        total_pt_ms     += pt.elapsed_ms;

#ifdef ENABLE_MPI
        int cmd = MPI_CMD_CALC_CORR;
        MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);
        corr_result_t mpi_res = run_corr_mpi(pairs, n);
        total_mpi_ms += mpi_res.elapsed_ms;
        char mpi_entry[256];
        snprintf(mpi_entry, sizeof(mpi_entry),
            "{\"r\":%.6f,\"elapsed_ms\":%.4f,\"threads_used\":%d,\"slope\":%.6f,\"intercept\":%.6f}",
            mpi_res.correlation_coefficient, mpi_res.elapsed_ms, mpi_res.threads_used,
            mpi_res.best_fit_slope, mpi_res.best_fit_intercept);
        const char *mpi_json = mpi_entry;
#else
        const char *mpi_json = "null";
#endif
        free(pairs);

        char entry[1024];
        snprintf(entry, sizeof(entry),
            "%s{"
            "\"subject\":\"%s\","
            "\"n_pairs\":%d,"
            "\"serial\":{\"r\":%.6f,\"elapsed_ms\":%.4f,\"threads_used\":%d,\"slope\":%.6f,\"intercept\":%.6f},"
            "\"parallel\":{\"r\":%.6f,\"elapsed_ms\":%.4f,\"threads_used\":%d,\"slope\":%.6f,\"intercept\":%.6f},"
            "\"pthread\":{\"r\":%.6f,\"elapsed_ms\":%.4f,\"threads_used\":%d,\"slope\":%.6f,\"intercept\":%.6f},"
            "\"mpi\":%s"
            "}",
            (processed > 0 ? "," : ""),
            subjects[i], n,
            serial.correlation_coefficient, serial.elapsed_ms, serial.threads_used, serial.best_fit_slope, serial.best_fit_intercept,
            par.correlation_coefficient,    par.elapsed_ms,    par.threads_used,    par.best_fit_slope,    par.best_fit_intercept,
            pt.correlation_coefficient,     pt.elapsed_ms,     pt.threads_used,     pt.best_fit_slope,     pt.best_fit_intercept,
            mpi_json);

        cc_buf_append(&subj_buf, &subj_len, &subj_cap, entry);
        processed++;
    }

    omp_set_num_threads(prev); g_num_threads = saved_g;
    pthread_mutex_unlock(&calc_lock);
    for (int i = 0; i < subject_count; i++) free(subjects[i]);
    free(subjects);

    if (processed == 0) {
        free(subj_buf);
        return SendErrorResponse(conn, 404,
            "No paired scores found. Ensure the reference subject has scores paired with other subjects.");
    }

    double speedup_par = (total_par_ms > 0) ? total_serial_ms / total_par_ms : 0.0;
    double speedup_pt  = (total_pt_ms  > 0) ? total_serial_ms / total_pt_ms  : 0.0;
    double speedup_mpi = (total_mpi_ms > 0) ? total_serial_ms / total_mpi_ms : 0.0;

    size_t data_sz = subj_len + 512;
    char  *data    = (char *)malloc(data_sz);
    if (!data) { free(subj_buf); return SendErrorResponse(conn, 500, "Memory allocation failed"); }

    snprintf(data, data_sz,
        "{"
        "\"reference_subject\":\"%s\","
        "\"class_name\":\"%s\","
        "\"threads_used\":%d,"
        "\"subjects\":[%s],"
        "\"timing\":{"
        "\"serial_total_ms\":%.4f,"
        "\"parallel_total_ms\":%.4f,"
        "\"pthread_total_ms\":%.4f,"
        "\"mpi_total_ms\":%.4f,"
        "\"db_fetch_ms\":%.4f,"
        "\"speedup_parallel\":%.4f,"
        "\"speedup_pthread\":%.4f,"
        "\"speedup_mpi\":%.4f"
        "}"
        "}",
        ref_subject, class_name, tval,
        subj_buf,
        total_serial_ms, total_par_ms, total_pt_ms, total_mpi_ms, total_db_ms,
        speedup_par, speedup_pt, speedup_mpi);

    free(subj_buf);
    int ret = SendJSONResponse(conn, "success", "One vs All correlation completed", data);
    free(data);
    return ret;
}
