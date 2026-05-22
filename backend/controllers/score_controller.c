

#include "score_controller.h"
#include "response_helper.h"
#include "calc_engine.h"
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
#include "calc_mpi.h"
#endif

extern db_connection_t *global_db;
extern int g_num_threads;

static int buf_append(char **buf, size_t *len, size_t *cap, const char *src)
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

    int  num_students  = 100;
    char class_name[256] = "";

    char buffer[1024];
    int  total_read = 0, n;
    while ((n = mg_read(conn, buffer + total_read,
                        (int)sizeof(buffer) - 1 - total_read)) > 0)
        total_read += n;
    int  dlen = total_read;
    if (dlen > 0) {
        buffer[dlen] = '\0';
        char *p;
        if ((p = strstr(buffer, "\"num_students\"")) != NULL) {
            p = strchr(p, ':');
            if (p) num_students = atoi(p + 1);
        }
        if ((p = strstr(buffer, "\"class_name\"")) != NULL) {
            p = strchr(p, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '"') {
                    p++;
                    char *end = strchr(p, '"');
                    if (end) {
                        size_t len = (size_t)(end - p);
                        if (len >= sizeof(class_name)) len = sizeof(class_name) - 1;
                        memcpy(class_name, p, len);
                        class_name[len] = '\0';
                    }
                }
            }
        }
    }
    if (num_students < 1) num_students = 100;

    const char *filter = class_name[0] ? class_name : NULL;
    int total = db_seed_dummy_data(global_db, num_students, 0, filter);
    if (total < 0)
        return SendErrorResponse(conn, 404,
            "Specified class not found. Create the class first.");

    char data[640];
    if (filter) {
        snprintf(data, sizeof(data),
            "{\n"
            "    \"students_created\": %d,\n"
            "    \"scores_created\": %d,\n"
            "    \"class_name\": \"%s\"\n"
            "  }",
            num_students, total, class_name);
    } else {
        snprintf(data, sizeof(data),
            "{\n"
            "    \"students_created\": %d,\n"
            "    \"scores_created\": %d\n"
            "  }",
            num_students, total);
    }

    return SendJSONResponse(conn, "success", "Dummy data seeded successfully", data);
}

int RemoveDuplicatesHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "DELETE") != 0)
        return SendErrorResponse(conn, 405, "Only DELETE method supported");

    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int scores_removed = 0;
    int students_removed = db_remove_duplicate_students(global_db, &scores_removed);

    if (students_removed < 0)
        return SendErrorResponse(conn, 500, "Failed to remove duplicate students");

    char data[256];
    snprintf(data, sizeof(data),
        "{\n"
        "    \"students_removed\": %d,\n"
        "    \"scores_removed\": %d\n"
        "  }",
        students_removed, scores_removed);

    return SendJSONResponse(conn, "success", "Duplicate students removed", data);
}

/* ── Helper to extract query parameters ── */
static int get_thread_count(const struct mg_request_info *ri) {
    if (!ri->query_string) return 0;
    char buf[16] = "";
    mg_get_var(ri->query_string, strlen(ri->query_string), "threads", buf, sizeof(buf));
    int t = atoi(buf);
    return (t > 0 && t <= 256) ? t : 0;
}

static void get_filter_params(const struct mg_request_info *ri, char *cls, size_t clen, char *sub, size_t slen) {
    cls[0] = '\0';
    sub[0] = '\0';
    if (ri->query_string) {
        size_t ql = strlen(ri->query_string);
        mg_get_var(ri->query_string, ql, "class", cls, clen);
        mg_get_var(ri->query_string, ql, "subject", sub, slen);
    }
}

int CalcSerialHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char cls_filter[256], sub_filter[256];
    get_filter_params(ri, cls_filter, sizeof(cls_filter), sub_filter, sizeof(sub_filter));
    const char *c_ptr = cls_filter[0] ? cls_filter : NULL;
    const char *s_ptr = sub_filter[0] ? sub_filter : NULL;

    int     count  = 0;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, c_ptr, s_ptr, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        pthread_mutex_unlock(&calc_lock);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    calc_result_t r = run_serial(scores, count);
    omp_set_num_threads(prev);
    
    pthread_mutex_unlock(&calc_lock);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "serial", db_fetch_ms);
    return SendJSONResponse(conn, "success", "Serial calculation completed", data);
}

int CalcParallelHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char cls_filter[256], sub_filter[256];
    get_filter_params(ri, cls_filter, sizeof(cls_filter), sub_filter, sizeof(sub_filter));
    const char *c_ptr = cls_filter[0] ? cls_filter : NULL;
    const char *s_ptr = sub_filter[0] ? sub_filter : NULL;
    int req_threads = get_thread_count(ri);

    int     count  = 0;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, c_ptr, s_ptr, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        pthread_mutex_unlock(&calc_lock);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    int prev = omp_get_max_threads();
    if (req_threads > 0) omp_set_num_threads(req_threads);
    calc_result_t r = run_parallel(scores, count);
    omp_set_num_threads(prev);

    pthread_mutex_unlock(&calc_lock);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "parallel", db_fetch_ms);
    return SendJSONResponse(conn, "success",
        "Parallel (OpenMP) calculation completed", data);
}

int CalcPthreadHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char cls_filter[256], sub_filter[256];
    get_filter_params(ri, cls_filter, sizeof(cls_filter), sub_filter, sizeof(sub_filter));
    const char *c_ptr = cls_filter[0] ? cls_filter : NULL;
    const char *s_ptr = sub_filter[0] ? sub_filter : NULL;
    int req_threads = get_thread_count(ri);

    int     count  = 0;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, c_ptr, s_ptr, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        pthread_mutex_unlock(&calc_lock);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    int saved_threads = g_num_threads;
    if (req_threads > 0) g_num_threads = req_threads;
    calc_result_t r = run_pthread(scores, count);
    g_num_threads = saved_threads;

    pthread_mutex_unlock(&calc_lock);
    free(scores);

    if (r.count == -1) {
        return SendErrorResponse(conn, 500, "Memory allocation failed during pthread calculation");
    }

    char data[2048];
    format_result_json(data, sizeof(data), &r, "pthread", db_fetch_ms);
    return SendJSONResponse(conn, "success",
        "Pthreads calculation completed", data);
}

int CalcCompareHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char cls_filter[256], sub_filter[256];
    get_filter_params(ri, cls_filter, sizeof(cls_filter), sub_filter, sizeof(sub_filter));
    const char *c_ptr = cls_filter[0] ? cls_filter : NULL;
    const char *s_ptr = sub_filter[0] ? sub_filter : NULL;

    int     count  = 0;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, c_ptr, s_ptr, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        pthread_mutex_unlock(&calc_lock);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    int req_threads = get_thread_count(ri);
    int prev = omp_get_max_threads();
    int saved_threads = g_num_threads;
    omp_set_num_threads(1);
    calc_result_t serial = run_serial(scores, count);

    if (req_threads > 0) {
        omp_set_num_threads(req_threads);
        g_num_threads = req_threads;
    } else {
        omp_set_num_threads(prev);
    }
    calc_result_t parallel = run_parallel(scores, count);
    calc_result_t pt_res   = run_pthread(scores, count);
    omp_set_num_threads(prev);
    g_num_threads = saved_threads;

    /* Parse optional MPI process count */
    const char *cmp_qs = ri->query_string ? ri->query_string : "";
    char cmp_mpi_str[16] = "";
    mg_get_var(cmp_qs, strlen(cmp_qs), "mpi_processes", cmp_mpi_str, sizeof(cmp_mpi_str));
    int cmp_req_mpi = atoi(cmp_mpi_str);

    double mpi_time_ms = 0.0;
    double speedup_mpi = 0.0;
    int mpi_threads = 0;
#ifdef ENABLE_MPI
    calc_result_t mpi_res;
    memset(&mpi_res, 0, sizeof(mpi_res));
    {
        int cmd = MPI_CMD_CALC_SCORES;
        MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);
        mpi_res = run_mpi(scores, count, cmp_req_mpi);
        mpi_time_ms = mpi_res.elapsed_ms;
        speedup_mpi = (mpi_time_ms > 0) ? serial.elapsed_ms / mpi_time_ms : 0.0;
        mpi_threads = mpi_res.threads_used;
    }
#endif

    pthread_mutex_unlock(&calc_lock);
    free(scores);

    double speedup = (parallel.elapsed_ms > 0)
        ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;
    double speedup_pt = (pt_res.elapsed_ms > 0)
        ? serial.elapsed_ms / pt_res.elapsed_ms : 0.0;

    char ser_json[2048], par_json[2048], pt_json[2048], mpi_json[2048];
    format_result_json(ser_json, sizeof(ser_json), &serial,   "serial",   db_fetch_ms);
    format_result_json(par_json, sizeof(par_json), &parallel, "parallel", db_fetch_ms);
    format_result_json(pt_json,  sizeof(pt_json),  &pt_res,   "pthread",  db_fetch_ms);
#ifdef ENABLE_MPI
    format_result_json(mpi_json, sizeof(mpi_json), &mpi_res,  "mpi",      db_fetch_ms);
#else
    strcpy(mpi_json, "null");
#endif

    char *data = (char *)malloc(16384);
    if (!data) return SendErrorResponse(conn, 500, "Memory allocation failed");

    snprintf(data, 16384,
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
        "      \"data_size\": %d,\n"
        "      \"improvement_pct\": %.2f\n"
        "    }\n"
        "  }",
        ser_json, par_json, pt_json, mpi_json,
        serial.elapsed_ms, parallel.elapsed_ms, pt_res.elapsed_ms, mpi_time_ms, db_fetch_ms,
        speedup, speedup_pt, speedup_mpi,
        serial.threads_used, parallel.threads_used, pt_res.threads_used, mpi_threads,
        count,
        (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0);

    int result = SendJSONResponse(conn, "success",
        "Serial vs Parallel vs Pthread vs MPI comparison completed", data);
    free(data);
    return result;
}

int CalcClassCompareHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char class_filter[256] = "";
    int ql = ri->query_string ? strlen(ri->query_string) : 0;
    if (ri->query_string) {
        mg_get_var(ri->query_string, ql, "class", class_filter, sizeof(class_filter));
    }
    if (class_filter[0] == '\0') {
        return SendErrorResponse(conn, 400, "Missing required query parameter: class");
    }
    int req_threads = get_thread_count(ri);

    char cls_mpi_str[16] = "";
    mg_get_var(ri->query_string ? ri->query_string : "", ql, "mpi_processes", cls_mpi_str, sizeof(cls_mpi_str));
    int cls_req_mpi = atoi(cls_mpi_str);

    char **subjects = NULL;
    int subject_count = db_get_class_subject_names(global_db, class_filter, &subjects);
    if (subject_count == 0) {
        char empty_resp[1024];
        snprintf(empty_resp, sizeof(empty_resp),
            "{\n"
            "  \"class\": \"%s\",\n"
            "  \"subjects\": [],\n"
            "  \"warning\": \"No subjects found\"\n"
            "}", class_filter);
        return SendJSONResponse(conn, "success", "No subjects found", empty_resp);
    }

    size_t out_cap = 16384, out_len = 0;
    char *out_buf = (char *)malloc(out_cap);
    if (!out_buf) {
        for (int i = 0; i < subject_count; i++) free(subjects[i]);
        free(subjects);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }
    out_buf[0] = '\0';
    buf_append(&out_buf, &out_len, &out_cap, "[\n");

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        for (int i = 0; i < subject_count; i++) free(subjects[i]);
        free(subjects);
        free(out_buf);
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    int prev_threads = omp_get_max_threads();
    int saved_g_threads = g_num_threads;

    for (int s_idx = 0; s_idx < subject_count; s_idx++) {
        const char *subject_name = subjects[s_idx];
        int count = 0;
        double t_db = omp_get_wtime();
        double *scores = db_get_scores_array(global_db, class_filter, subject_name, &count);
        double db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;

        if (!scores || count == 0) {
            if (scores) free(scores);
            continue;
        }

        omp_set_num_threads(1);
        calc_result_t serial = run_serial(scores, count);

        if (req_threads > 0) {
            omp_set_num_threads(req_threads);
            g_num_threads = req_threads;
        } else {
            omp_set_num_threads(prev_threads);
        }
        calc_result_t parallel = run_parallel(scores, count);
        calc_result_t pt_res   = run_pthread(scores, count);

#ifdef ENABLE_MPI
        int cmd = MPI_CMD_CALC_SCORES;
        MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);
        calc_result_t mpi_res = run_mpi(scores, count, cls_req_mpi);
        double mpi_time_ms = mpi_res.elapsed_ms;
        double speedup_mpi = (mpi_time_ms > 0) ? serial.elapsed_ms / mpi_time_ms : 0.0;
        int mpi_threads = mpi_res.threads_used;
        char mpi_json[2048];
        if (mpi_res.elapsed_ms < 0.0) {
            snprintf(mpi_json, sizeof(mpi_json), "null,\n      \"mpi_error\": \"MPI workers are not running\"");
        } else {
            char m_json[1024];
            format_result_json(m_json, sizeof(m_json), &mpi_res, "mpi", db_fetch_ms);
            snprintf(mpi_json, sizeof(mpi_json), "%s", m_json);
        }
#else
        double mpi_time_ms = 0.0;
        double speedup_mpi = 0.0;
        int mpi_threads = 0;
        char mpi_json[256];
        snprintf(mpi_json, sizeof(mpi_json), "null,\n      \"mpi_error\": \"MPI not enabled\"");
#endif
        free(scores);

        double speedup = (parallel.elapsed_ms > 0) ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;
        double speedup_pt = (pt_res.elapsed_ms > 0) ? serial.elapsed_ms / pt_res.elapsed_ms : 0.0;

        char ser_json[2048], par_json[2048], pt_json[2048];
        format_result_json(ser_json, sizeof(ser_json), &serial, "serial", db_fetch_ms);
        format_result_json(par_json, sizeof(par_json), &parallel, "parallel", db_fetch_ms);
        format_result_json(pt_json,  sizeof(pt_json),  &pt_res,  "pthread",  db_fetch_ms);

        char subj_json[16384];
        snprintf(subj_json, sizeof(subj_json),
            "    {\n"
            "      \"subject\": \"%s\",\n"
            "      \"serial\": %s,\n"
            "      \"parallel\": %s,\n"
            "      \"pthread\": %s,\n"
            "      \"mpi\": %s,\n"
            "      \"comparison\": {\n"
            "        \"serial_time_ms\": %.4f,\n"
            "        \"parallel_time_ms\": %.4f,\n"
            "        \"pthread_time_ms\": %.4f,\n"
            "        \"mpi_time_ms\": %.4f,\n"
            "        \"db_fetch_ms\": %.4f,\n"
            "        \"speedup\": %.4f,\n"
            "        \"speedup_pthread\": %.4f,\n"
            "        \"speedup_mpi\": %.4f,\n"
            "        \"serial_threads\": %d,\n"
            "        \"parallel_threads\": %d,\n"
            "        \"pthread_threads\": %d,\n"
            "        \"mpi_threads\": %d,\n"
            "        \"data_size\": %d,\n"
            "        \"improvement_pct\": %.2f\n"
            "      }\n"
            "    }",
            subject_name,
            ser_json, par_json, pt_json, mpi_json,
            serial.elapsed_ms, parallel.elapsed_ms, pt_res.elapsed_ms, mpi_time_ms, db_fetch_ms,
            speedup, speedup_pt, speedup_mpi,
            serial.threads_used, parallel.threads_used, pt_res.threads_used, mpi_threads,
            count,
            (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0);

        if (s_idx > 0 && out_len > 3) {
            buf_append(&out_buf, &out_len, &out_cap, ",\n");
        }
        buf_append(&out_buf, &out_len, &out_cap, subj_json);
    }

    omp_set_num_threads(prev_threads);
    g_num_threads = saved_g_threads;
    pthread_mutex_unlock(&calc_lock);
    buf_append(&out_buf, &out_len, &out_cap, "\n  ]");

    size_t full_cap = out_len + 1024;
    char *full_data = (char *)malloc(full_cap);
    if (!full_data) {
        free(out_buf);
        for (int i = 0; i < subject_count; i++) free(subjects[i]);
        free(subjects);
        return SendErrorResponse(conn, 500, "Memory allocation failed");
    }

    snprintf(full_data, full_cap,
        "{\n"
        "  \"class\": \"%s\",\n"
        "  \"subjects\": %s\n"
        "}",
        class_filter, out_buf);

    int result = SendJSONResponse(conn, "success", "Class comparison completed", full_data);

    free(full_data);
    free(out_buf);
    for (int i = 0; i < subject_count; i++) free(subjects[i]);
    free(subjects);

    return result;
}

/* ── MPI distributed score calculation ─────────────────────────────────── */
#ifdef ENABLE_MPI

int CalcMpiHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    char cls_filter[256], sub_filter[256];
    get_filter_params(ri, cls_filter, sizeof(cls_filter), sub_filter, sizeof(sub_filter));
    const char *c_ptr = cls_filter[0] ? cls_filter : NULL;
    const char *s_ptr = sub_filter[0] ? sub_filter : NULL;

    int     count  = 0;

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 30;
    if (pthread_mutex_timedlock(&calc_lock, &timeout) == ETIMEDOUT) {
        return SendErrorResponse(conn, 503, "Server busy, calculation in progress");
    }

    double  t_db   = omp_get_wtime();
    double *scores = db_get_scores_array(global_db, c_ptr, s_ptr, &count);
    double  db_fetch_ms = (omp_get_wtime() - t_db) * 1000.0;
    if (!scores || count == 0) {
        if (scores) free(scores);
        pthread_mutex_unlock(&calc_lock);
        return SendErrorResponse(conn, 404,
            "No scores in database. POST /api/seed first.");
    }

    /* Parse optional process count */
    const char *mpi_qs  = ri->query_string ? ri->query_string : "";
    size_t      mpi_qsl = strlen(mpi_qs);
    char mpi_proc_str[16] = "";
    mg_get_var(mpi_qs, mpi_qsl, "mpi_processes", mpi_proc_str, sizeof(mpi_proc_str));
    int req_mpi = atoi(mpi_proc_str);

    /* Signal workers to participate in score calculation */
    int cmd = MPI_CMD_CALC_SCORES;
    MPI_Bcast(&cmd, 1, MPI_INT, 0, MPI_COMM_WORLD);

    calc_result_t r = run_mpi(scores, count, req_mpi);

    pthread_mutex_unlock(&calc_lock);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "mpi", db_fetch_ms);
    return SendJSONResponse(conn, "success",
        "MPI distributed calculation completed", data);
}
#endif /* ENABLE_MPI */
