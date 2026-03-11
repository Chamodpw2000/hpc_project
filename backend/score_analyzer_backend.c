/*
 * Score Analyzer Backend
 * Copyright (c) 2026
 * MIT License
 */

/* Students Score Management Engine API with health check functionality */

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <omp.h>

#include "civetweb.h"
#include "config.h"
#include "db.h"

#define PORT "8090"
#define HOST_INFO "http://localhost:8090"

// API Endpoints
#define ROOT_URI "/"
#define HEALTH_URI "/health"
#define API_URI "/api/*"
#define USERS_URI "/api/students"
#define USERS_ID_URI "/api/students/*"
#define DATA_URI "/api/data"
#define TEST_URI "/api/test/*"
#define EXIT_URI "/exit"
#define SEED_URI "/api/seed"
#define CALC_SERIAL_URI "/api/calculate/serial"
#define CALC_PARALLEL_URI "/api/calculate/parallel"
#define CALC_COMPARE_URI "/api/calculate/compare"
#define CALC_POSIX_URI "/api/calculate/posix"

int exitNow = 0;
static unsigned requestCounter = 0;
static db_connection_t *global_db = NULL;

// Simple JSON response structure (without external cJSON dependency)
static int
SendJSONResponse(struct mg_connection *conn, const char *status, const char *message, const char *data)
{
    size_t data_len = data ? strlen(data) : 0;
    size_t buf_size = 512 + data_len;
    char *response = (char*)malloc(buf_size);
    if (!response) return 500;
    time_t now = time(NULL);
    
    snprintf(response, buf_size,
        "{\n"
        "  \"status\": \"%s\",\n"
        "  \"message\": \"%s\",\n"
        "  \"timestamp\": %ld,\n"
        "  \"request_count\": %u%s%s\n"
        "}",
        status, message, now, ++requestCounter,
        data ? ",\n  \"data\": " : "",
        data ? data : "");
    
    size_t response_len = strlen(response);
    
    /* Send HTTP message header + CORS */
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Connection: close\r\n\r\n",
              response_len);
    
    /* Send HTTP message content */
    mg_write(conn, response, response_len);
    free(response);
    
    return 200;
}

static int
SendErrorResponse(struct mg_connection *conn, int status_code, const char *message)
{
    char response[1024];
    time_t now = time(NULL);
    
    snprintf(response, sizeof(response),
        "{\n"
        "  \"status\": \"error\",\n"
        "  \"message\": \"%s\",\n"
        "  \"timestamp\": %ld,\n"
        "  \"error_code\": %d\n"
        "}",
        message, now, status_code);
    
    size_t response_len = strlen(response);
    
    /* Send HTTP error response */
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Connection: close\r\n\r\n",
              status_code,
              (status_code == 404) ? "Not Found" :
              (status_code == 405) ? "Method Not Allowed" :
              (status_code == 400) ? "Bad Request" :
              (status_code == 500) ? "Internal Server Error" : "Error",
              response_len);
    
    mg_write(conn, response, response_len);
    
    return status_code;
}

// Root Handler - Welcome page
static int
RootHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    
    printf("Root page request from %s\n", ri->remote_addr);
    
    if (0 == strcmp(ri->request_method, "GET")) {
        char welcome_data[512];
        snprintf(welcome_data, sizeof(welcome_data),
            "{\n"
            "    \"title\": \"Students Score Management Engine\",\n"
            "    \"version\": \"%s\",\n"
            "    \"port\": \"%s\",\n"
            "    \"endpoints\": {\n"
            "      \"health\": \"%s/health\",\n"
            "      \"api\": \"%s/api\",\n"
            "      \"students\": \"%s/api/students\",\n"
            "      \"data\": \"%s/api/data\"\n"
            "    }\n"
            "  }",
            CIVETWEB_VERSION, PORT, HOST_INFO, HOST_INFO, HOST_INFO, HOST_INFO);
        
        return SendJSONResponse(conn, "running", 
            "Welcome to Students Score Management Engine - Your server is running on port " PORT, 
            welcome_data);
    }
    
    return SendErrorResponse(conn, 405, "Only GET method supported for root endpoint");
}

// Health Check Handler - Always returns server status
static int
HealthHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    
    printf("Health check request from %s\n", ri->remote_addr);
    
    if (0 == strcmp(ri->request_method, "GET")) {
        char health_data[1024];
        snprintf(health_data, sizeof(health_data),
            "{\n"
            "    \"server\": \"CivetWeb Test API\",\n"
            "    \"version\": \"%s\",\n"
            "    \"uptime_requests\": %u,\n"
            "    \"port\": \"%s\",\n"
            "    \"remote_addr\": \"%s\"\n"
            "  }",
            CIVETWEB_VERSION, requestCounter, PORT, ri->remote_addr);
        
        return SendJSONResponse(conn, "healthy", "Server is running normally", health_data);
    }
    
    return SendErrorResponse(conn, 405, "Only GET method supported for health check");
}

// Simple JSON parser helper functions
static char* extract_json_value(const char* json, const char* key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    
    char* key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    
    key_pos += strlen(search_key);
    while (*key_pos == ' ' || *key_pos == '\t') key_pos++; // skip whitespace
    
    if (*key_pos != '"') return NULL; // expect quoted value
    key_pos++; // skip opening quote
    
    char* end_quote = strchr(key_pos, '"');
    if (!end_quote) return NULL;
    
    size_t value_len = end_quote - key_pos;
    char* value = malloc(value_len + 1);
    strncpy(value, key_pos, value_len);
    value[value_len] = '\0';
    
    return value;
}

// Students API Handler - MongoDB integrated student management
static int
UsersHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *url = ri->local_uri;
    char userid[256] = "";
    
    printf("Students API: %s %s\n", ri->request_method, url);
    
    // Check database connection
    if (!global_db) {
        return SendErrorResponse(conn, 500, "Database connection not available");
    }
    
    // Extract student ID if present (/api/students/123)
    if (strncmp(url, "/api/students/", 14) == 0) {
        const char *id_start = url + 14;
        if (*id_start != '\0') {
            strncpy(userid, id_start, sizeof(userid) - 1);
            userid[sizeof(userid) - 1] = '\0';
        }
    }
    
    if (0 == strcmp(ri->request_method, "GET")) {
        if (strlen(userid) > 0) {
            // GET specific user
            char *user_json = db_get_student_by_id(global_db, userid);
            if (user_json && strcmp(user_json, "null") != 0) {
                int result = SendJSONResponse(conn, "success", "User retrieved successfully", user_json);
                free(user_json);
                return result;
            } else {
                if (user_json) free(user_json);
                return SendErrorResponse(conn, 404, "User not found");
            }
        } else {
            // GET all students
            char *users_json = db_get_all_students(global_db);
            int result = SendJSONResponse(conn, "success", "Students retrieved successfully", users_json);
            free(users_json);
            return result;
        }
    }
    else if (0 == strcmp(ri->request_method, "POST")) {
        // Create new user
        char buffer[1024];
        int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen <= 0) {
            return SendErrorResponse(conn, 400, "No data received");
        }
        
        buffer[dlen] = '\0';
        printf("Received POST data: %s\n", buffer);
        
        // Parse JSON to extract name, email, and student_id
        char *name = extract_json_value(buffer, "name");
        char *email = extract_json_value(buffer, "email");
        char *student_id = extract_json_value(buffer, "student_id");
        
        if (!name || !email || !student_id) {
            if (name) free(name);
            if (email) free(email);
            if (student_id) free(student_id);
            return SendErrorResponse(conn, 400, "Missing required fields: name, email, student_id");
        }
        
        // Create user in database
        int success = db_create_student(global_db, name, email, student_id);
        
        if (success) {
            // Return the created user data
            char *created_user_json = db_get_student_by_id(global_db, student_id);
            int result = SendJSONResponse(conn, "success", "User created successfully", created_user_json);
            free(created_user_json);
            free(name);
            free(email);
            free(student_id);
            return result;
        } else {
            free(name);
            free(email);
            free(student_id);
            return SendErrorResponse(conn, 500, "Failed to create user in database");
        }
    }
    else if (0 == strcmp(ri->request_method, "PUT")) {
        if (strlen(userid) == 0) {
            return SendErrorResponse(conn, 400, "User ID required for PUT request");
        }
        
        char buffer[1024];
        int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen <= 0) {
            return SendErrorResponse(conn, 400, "No data received");
        }
        
        buffer[dlen] = '\0';
        printf("Received PUT data for user %s: %s\n", userid, buffer);
        
        // Parse JSON to extract name and email
        char *name = extract_json_value(buffer, "name");
        char *email = extract_json_value(buffer, "email");
        
        if (!name || !email) {
            if (name) free(name);
            if (email) free(email);
            return SendErrorResponse(conn, 400, "Missing required fields: name, email");
        }
        
        // Update user in database
        int success = db_update_student(global_db, userid, name, email);
        
        if (success) {
            // Return updated user data
            char *updated_user_json = db_get_student_by_id(global_db, userid);
            int result = SendJSONResponse(conn, "success", "User updated successfully", updated_user_json);
            free(updated_user_json);
            free(name);
            free(email);
            return result;
        } else {
            free(name);
            free(email);
            return SendErrorResponse(conn, 500, "Failed to update user in database");
        }
    }
    else if (0 == strcmp(ri->request_method, "DELETE")) {
        if (strlen(userid) == 0) {
            return SendErrorResponse(conn, 400, "User ID required for DELETE request");
        }
        
        printf("DELETE user %s\n", userid);
        
        // Delete user from database
        int success = db_delete_student(global_db, userid);
        
        if (success) {
            mg_printf(conn,
                      "HTTP/1.1 204 No Content\r\n"
                      "Connection: close\r\n\r\n");
            return 204;
        } else {
            return SendErrorResponse(conn, 500, "Failed to delete user from database");
        }
    }
    
    return SendErrorResponse(conn, 405, "Method not supported for students endpoint");
}

// Generic Data Handler - For testing various data operations
static int
DataHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    
    printf("Data API: %s %s\n", ri->request_method, ri->local_uri);
    
    if (0 == strcmp(ri->request_method, "GET")) {
        const char *sample_data = 
            "{\n"
            "    \"items\": [\n"
            "      {\"id\": 1, \"value\": \"apple\", \"category\": \"fruit\"},\n"
            "      {\"id\": 2, \"value\": \"carrot\", \"category\": \"vegetable\"},\n"
            "      {\"id\": 3, \"value\": \"banana\", \"category\": \"fruit\"}\n"
            "    ],\n"
            "    \"total\": 3\n"
            "  }";
        return SendJSONResponse(conn, "success", "Data retrieved successfully", sample_data);
    }
    else if (0 == strcmp(ri->request_method, "POST")) {
        char buffer[1024];
        int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen > 0) {
            buffer[dlen] = '\0';
            printf("Received data: %s\n", buffer);
        }
        
        return SendJSONResponse(conn, "success", "Data processed successfully", "null");
    }
    
    return SendErrorResponse(conn, 405, "Only GET and POST methods supported for data endpoint");
}

// Test Handler - For various testing scenarios
static int
TestHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *url = ri->local_uri;
    char test_type[256] = "";
    
    // Extract test type (/api/test/performance, /api/test/error, etc.)
    if (strncmp(url, "/api/test/", 10) == 0) {
        const char *type_start = url + 10;
        if (*type_start != '\0') {
            strncpy(test_type, type_start, sizeof(test_type) - 1);
            test_type[sizeof(test_type) - 1] = '\0';
        }
    }
    
    printf("Test API: %s %s (type: %s)\n", ri->request_method, url, test_type);
    
    if (strlen(test_type) == 0) {
        const char *available_tests = 
            "[\n"
            "    \"performance\",\n"
            "    \"error\",\n"
            "    \"delay\",\n"
            "    \"large-response\"\n"
            "  ]";
        return SendJSONResponse(conn, "success", "Available test endpoints", available_tests);
    }
    
    if (strcmp(test_type, "error") == 0) {
        return SendErrorResponse(conn, 500, "This is a test error response");
    }
    else if (strcmp(test_type, "delay") == 0) {
        printf("Simulating delay...\n");
#ifdef _WIN32
        Sleep(2000);  // 2 seconds
#else
        sleep(2);     // 2 seconds
#endif
        return SendJSONResponse(conn, "success", "Delayed response completed", "null");
    }
    else if (strcmp(test_type, "large-response") == 0) {
        // Generate a larger response for testing
        char large_data[4096];
        snprintf(large_data, sizeof(large_data),
            "{\n"
            "    \"description\": \"Large test response\",\n"
            "    \"data\": \"%s\",\n"
            "    \"repeat_count\": 100\n"
            "  }",
            "This is a test string that will be repeated to create a larger response payload for testing purposes. ");
        return SendJSONResponse(conn, "success", "Large response generated", large_data);
    }
    else if (strcmp(test_type, "performance") == 0) {
        clock_t start = clock();
        
        // Simulate some processing
        volatile int dummy = 0;
        for (int i = 0; i < 100000; i++) {
            dummy += i;
        }
        
        clock_t end = clock();
        double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
        
        char perf_data[512];
        snprintf(perf_data, sizeof(perf_data),
            "{\n"
            "    \"processing_time_seconds\": %.6f,\n"
            "    \"dummy_result\": %d\n"
            "  }",
            cpu_time, dummy);
        return SendJSONResponse(conn, "success", "Performance test completed", perf_data);
    }
    
    return SendErrorResponse(conn, 404, "Unknown test type");
}

/*
 * =============================================================
 *  OpenMP Parallel vs Serial Score Analysis
 * =============================================================
 */

/* Grade distribution buckets: A(>=90), B(>=80), C(>=70), D(>=60), F(<60) */
typedef struct {
    double sum;
    double mean;
    double variance;
    double stddev;
    double min;
    double max;
    int grade_A;
    int grade_B;
    int grade_C;
    int grade_D;
    int grade_F;
    double sort_time_ms;     /* time spent sorting */
    double median;
    int count;
    double elapsed_ms;       /* wall-clock time */
    int threads_used;
} calc_result_t;

/* Compare function for qsort */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

/* --- SERIAL calculation ------------------------------------------------- */
static calc_result_t run_serial(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count = n;
    r.threads_used = 1;

    double t_start = omp_get_wtime();

    /* Sum, min, max */
    r.min = scores[0];
    r.max = scores[0];
    r.sum = 0.0;
    for (int i = 0; i < n; i++) {
        r.sum += scores[i];
        if (scores[i] < r.min) r.min = scores[i];
        if (scores[i] > r.max) r.max = scores[i];
    }
    r.mean = r.sum / n;

    /* Variance + grade distribution */
    double var_sum = 0.0;
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if (scores[i] >= 90) r.grade_A++;
        else if (scores[i] >= 80) r.grade_B++;
        else if (scores[i] >= 70) r.grade_C++;
        else if (scores[i] >= 60) r.grade_D++;
        else r.grade_F++;
    }
    r.variance = var_sum / n;
    r.stddev = sqrt(r.variance);

    /* Median (need sorted copy) */
    double *sorted = (double*)malloc(sizeof(double) * n);
    memcpy(sorted, scores, sizeof(double) * n);

    double sort_start = omp_get_wtime();
    qsort(sorted, n, sizeof(double), cmp_double);
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    if (n % 2 == 0)
        r.median = (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    else
        r.median = sorted[n/2];
    free(sorted);

    /* Intensive extra work to amplify difference: repeated dot-products */
    volatile double dummy = 0.0;
    for (int rep = 0; rep < 50; rep++) {
        for (int i = 0; i < n; i++) {
            dummy += sin(scores[i]) * cos(scores[i]);
        }
    }
    (void)dummy;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* --- PARALLEL (OpenMP) calculation ------------------------------------- */

/* Parallel merge-sort helpers */
static void merge(double *arr, int l, int m, int r)
{
    int n1 = m - l + 1, n2 = r - m;
    double *L = malloc(sizeof(double) * n1);
    double *R = malloc(sizeof(double) * n2);
    memcpy(L, arr + l, sizeof(double) * n1);
    memcpy(R, arr + m + 1, sizeof(double) * n2);

    int i = 0, j = 0, k = l;
    while (i < n1 && j < n2) arr[k++] = (L[i] <= R[j]) ? L[i++] : R[j++];
    while (i < n1) arr[k++] = L[i++];
    while (j < n2) arr[k++] = R[j++];
    free(L); free(R);
}

static void parallel_merge_sort(double *arr, int l, int r, int depth)
{
    if (l >= r) return;
    int m = l + (r - l) / 2;

    if (depth < 4) {
        #pragma omp task shared(arr) if(depth < 4)
        parallel_merge_sort(arr, l, m, depth + 1);
        #pragma omp task shared(arr) if(depth < 4)
        parallel_merge_sort(arr, m + 1, r, depth + 1);
        #pragma omp taskwait
    } else {
        parallel_merge_sort(arr, l, m, depth + 1);
        parallel_merge_sort(arr, m + 1, r, depth + 1);
    }
    merge(arr, l, m, r);
}

static calc_result_t run_parallel(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count = n;
    r.threads_used = omp_get_max_threads();

    double t_start = omp_get_wtime();

    /* Parallel sum, min, max */
    double p_sum = 0.0, p_min = scores[0], p_max = scores[0];

    #pragma omp parallel for reduction(+:p_sum) reduction(min:p_min) reduction(max:p_max) schedule(static)
    for (int i = 0; i < n; i++) {
        p_sum += scores[i];
        if (scores[i] < p_min) p_min = scores[i];
        if (scores[i] > p_max) p_max = scores[i];
    }
    r.sum = p_sum;
    r.min = p_min;
    r.max = p_max;
    r.mean = r.sum / n;

    /* Parallel variance + grade distribution */
    double var_sum = 0.0;
    int gA = 0, gB = 0, gC = 0, gD = 0, gF = 0;

    #pragma omp parallel for reduction(+:var_sum,gA,gB,gC,gD,gF) schedule(static)
    for (int i = 0; i < n; i++) {
        double diff = scores[i] - r.mean;
        var_sum += diff * diff;

        if (scores[i] >= 90) gA++;
        else if (scores[i] >= 80) gB++;
        else if (scores[i] >= 70) gC++;
        else if (scores[i] >= 60) gD++;
        else gF++;
    }
    r.variance = var_sum / n;
    r.stddev = sqrt(r.variance);
    r.grade_A = gA; r.grade_B = gB; r.grade_C = gC;
    r.grade_D = gD; r.grade_F = gF;

    /* Parallel merge-sort for median */
    double *sorted = (double*)malloc(sizeof(double) * n);
    memcpy(sorted, scores, sizeof(double) * n);

    double sort_start = omp_get_wtime();
    #pragma omp parallel
    {
        #pragma omp single
        parallel_merge_sort(sorted, 0, n - 1, 0);
    }
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    if (n % 2 == 0)
        r.median = (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    else
        r.median = sorted[n/2];
    free(sorted);

    /* Intensive extra work - parallelised */
    volatile double dummy = 0.0;
    double local_dummy = 0.0;
    #pragma omp parallel for reduction(+:local_dummy) schedule(static)
    for (int rep = 0; rep < 50; rep++) {
        for (int i = 0; i < n; i++) {
            local_dummy += sin(scores[i]) * cos(scores[i]);
        }
    }
    dummy = local_dummy;
    (void)dummy;

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* --- POSIX (pthreads) calculation ------------------------------------- */

#include <pthread.h>

typedef struct {
    const double *scores;
    int start;
    int end;
    double mean;      /* input for phase 2 */
    
    /* outputs */
    double p_sum;
    double p_min;
    double p_max;
    
    double var_sum;
    int gA;
    int gB;
    int gC;
    int gD;
    int gF;
    
    double local_dummy;
} posix_worker_data_t;

/* Phase 1: Sum, min, max */
static void *posix_calc_worker_phase1(void *arg) {
    posix_worker_data_t *d = (posix_worker_data_t *)arg;
    
    if (d->start < d->end) {
        d->p_sum = 0.0;
        d->p_min = d->scores[d->start];
        d->p_max = d->scores[d->start];
        
        for (int i = d->start; i < d->end; i++) {
            d->p_sum += d->scores[i];
            if (d->scores[i] < d->p_min) d->p_min = d->scores[i];
            if (d->scores[i] > d->p_max) d->p_max = d->scores[i];
        }
    }
    return NULL;
}

/* Phase 2: Variance, grades, intensive loop */
static void *posix_calc_worker_phase2(void *arg) {
    posix_worker_data_t *d = (posix_worker_data_t *)arg;
    
    d->var_sum = 0.0;
    d->gA = 0; d->gB = 0; d->gC = 0; d->gD = 0; d->gF = 0;
    d->local_dummy = 0.0;
    
    for (int i = d->start; i < d->end; i++) {
        double diff = d->scores[i] - d->mean;
        d->var_sum += diff * diff;

        if (d->scores[i] >= 90) d->gA++;
        else if (d->scores[i] >= 80) d->gB++;
        else if (d->scores[i] >= 70) d->gC++;
        else if (d->scores[i] >= 60) d->gD++;
        else d->gF++;
    }
    
    /* Intensive extra work - parallelised */
    for (int rep = 0; rep < 50; rep++) {
        for (int i = d->start; i < d->end; i++) {
            d->local_dummy += sin(d->scores[i]) * cos(d->scores[i]);
        }
    }
    
    return NULL;
}

/* POSIX merge-sort thread struct */
typedef struct {
    double *arr;
    int l;
    int r;
    int depth;
} posix_sort_data_t;

static void *posix_merge_sort_thread(void *arg);

static void posix_merge_sort(double *arr, int l, int r, int depth) {
    if (l >= r) return;
    int m = l + (r - l) / 2;

    if (depth < 4) {
        pthread_t t1, t2;
        posix_sort_data_t d1 = {arr, l, m, depth + 1};
        posix_sort_data_t d2 = {arr, m + 1, r, depth + 1};
        
        pthread_create(&t1, NULL, posix_merge_sort_thread, &d1);
        pthread_create(&t2, NULL, posix_merge_sort_thread, &d2);
        
        pthread_join(t1, NULL);
        pthread_join(t2, NULL);
    } else {
        posix_merge_sort(arr, l, m, depth + 1);
        posix_merge_sort(arr, m + 1, r, depth + 1);
    }
    merge(arr, l, m, r);
}

static void *posix_merge_sort_thread(void *arg) {
    posix_sort_data_t *d = (posix_sort_data_t *)arg;
    posix_merge_sort(d->arr, d->l, d->r, d->depth);
    return NULL;
}

static calc_result_t run_posix(const double *scores, int n)
{
    calc_result_t r;
    memset(&r, 0, sizeof(r));
    r.count = n;
    
    /* Determine reasonable thread count */
    int num_threads = omp_get_max_threads();
    if (num_threads <= 0) num_threads = 4;
    r.threads_used = num_threads;

    double t_start = omp_get_wtime();
    
    pthread_t *threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    posix_worker_data_t *tdata = (posix_worker_data_t*)malloc(sizeof(posix_worker_data_t) * num_threads);

    /* Phase 1: sum, min, max */
    int chunk = n / num_threads;
    for (int i = 0; i < num_threads; i++) {
        tdata[i].scores = scores;
        tdata[i].start = i * chunk;
        tdata[i].end = (i == num_threads - 1) ? n : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, posix_calc_worker_phase1, &tdata[i]);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Aggregate Phase 1 */
    r.min = tdata[0].p_min;
    r.max = tdata[0].p_max;
    r.sum = 0.0;
    for (int i = 0; i < num_threads; i++) {
        r.sum += tdata[i].p_sum;
        if (tdata[i].p_min < r.min) r.min = tdata[i].p_min;
        if (tdata[i].p_max > r.max) r.max = tdata[i].p_max;
    }
    r.mean = r.sum / n;

    /* Phase 2: variance, grades, dummy work */
    for (int i = 0; i < num_threads; i++) {
        tdata[i].mean = r.mean;
        pthread_create(&threads[i], NULL, posix_calc_worker_phase2, &tdata[i]);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Aggregate Phase 2 */
    double total_var_sum = 0.0;
    int gA = 0, gB = 0, gC = 0, gD = 0, gF = 0;
    volatile double dummy = 0.0;
    
    for (int i = 0; i < num_threads; i++) {
        total_var_sum += tdata[i].var_sum;
        gA += tdata[i].gA;
        gB += tdata[i].gB;
        gC += tdata[i].gC;
        gD += tdata[i].gD;
        gF += tdata[i].gF;
        dummy += tdata[i].local_dummy;
    }
    (void)dummy;
    
    r.variance = total_var_sum / n;
    r.stddev = sqrt(r.variance);
    r.grade_A = gA; r.grade_B = gB; r.grade_C = gC;
    r.grade_D = gD; r.grade_F = gF;
    
    free(threads);
    free(tdata);

    /* POSIX merge-sort for median */
    double *sorted = (double*)malloc(sizeof(double) * n);
    memcpy(sorted, scores, sizeof(double) * n);

    double sort_start = omp_get_wtime();
    
    pthread_t sort_thread;
    posix_sort_data_t sdata = {sorted, 0, n - 1, 0};
    pthread_create(&sort_thread, NULL, posix_merge_sort_thread, &sdata);
    pthread_join(sort_thread, NULL);
    
    r.sort_time_ms = (omp_get_wtime() - sort_start) * 1000.0;

    if (n % 2 == 0)
        r.median = (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
    else
        r.median = sorted[n/2];
    free(sorted);

    r.elapsed_ms = (omp_get_wtime() - t_start) * 1000.0;
    return r;
}

/* Format a calc_result_t into a JSON string */
static void format_result_json(char *buf, size_t sz, const calc_result_t *r, const char *label)
{
    snprintf(buf, sz,
        "{\n"
        "    \"mode\": \"%s\",\n"
        "    \"threads_used\": %d,\n"
        "    \"scores_count\": %d,\n"
        "    \"elapsed_ms\": %.4f,\n"
        "    \"sort_time_ms\": %.4f,\n"
        "    \"statistics\": {\n"
        "      \"sum\": %.4f,\n"
        "      \"mean\": %.4f,\n"
        "      \"median\": %.4f,\n"
        "      \"variance\": %.4f,\n"
        "      \"stddev\": %.4f,\n"
        "      \"min\": %.4f,\n"
        "      \"max\": %.4f\n"
        "    },\n"
        "    \"grade_distribution\": {\n"
        "      \"A_90_100\": %d,\n"
        "      \"B_80_89\": %d,\n"
        "      \"C_70_79\": %d,\n"
        "      \"D_60_69\": %d,\n"
        "      \"F_below_60\": %d\n"
        "    }\n"
        "  }",
        label, r->threads_used, r->count, r->elapsed_ms, r->sort_time_ms,
        r->sum, r->mean, r->median, r->variance, r->stddev, r->min, r->max,
        r->grade_A, r->grade_B, r->grade_C, r->grade_D, r->grade_F);
}

/* ---- API: POST /api/seed  ----- */
static int
SeedHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "POST") != 0) {
        return SendErrorResponse(conn, 405, "Only POST method supported. Send POST with optional JSON {num_students, scores_per_student}");
    }

    if (!global_db) {
        return SendErrorResponse(conn, 500, "Database connection not available");
    }

    /* Defaults */
    int num_students = 100;
    int scores_per_student = 10;

    /* Read optional body */
    char buffer[512];
    int dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
    if (dlen > 0) {
        buffer[dlen] = '\0';
        /* Very simple int extraction */
        char *p;
        if ((p = strstr(buffer, "\"num_students\"")) != NULL) {
            p = strchr(p, ':');
            if (p) num_students = atoi(p + 1);
        }
        if ((p = strstr(buffer, "\"scores_per_student\"")) != NULL) {
            p = strchr(p, ':');
            if (p) scores_per_student = atoi(p + 1);
        }
    }
    if (num_students < 1) num_students = 100;
    if (scores_per_student < 1) scores_per_student = 10;

    int total = db_seed_dummy_data(global_db, num_students, scores_per_student);

    char data[512];
    snprintf(data, sizeof(data),
        "{\n"
        "    \"students_created\": %d,\n"
        "    \"scores_created\": %d,\n"
        "    \"scores_per_student\": %d\n"
        "  }",
        num_students, total, scores_per_student);

    return SendJSONResponse(conn, "success", "Dummy data seeded successfully", data);
}

/* ---- API: GET /api/calculate/serial  ----- */
static int
CalcSerialHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int count = 0;
    double *scores = db_get_scores_array(global_db, &count);
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404, "No scores in database. POST /api/seed first.");
    }

    /* Force single thread */
    int prev = omp_get_max_threads();
    omp_set_num_threads(1);

    calc_result_t r = run_serial(scores, count);
    free(scores);

    omp_set_num_threads(prev);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "serial");

    return SendJSONResponse(conn, "success", "Serial calculation completed", data);
}

/* ---- API: GET /api/calculate/parallel  ----- */
static int
CalcParallelHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int count = 0;
    double *scores = db_get_scores_array(global_db, &count);
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404, "No scores in database. POST /api/seed first.");
    }

    calc_result_t r = run_parallel(scores, count);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "parallel");

    return SendJSONResponse(conn, "success", "Parallel (OpenMP) calculation completed", data);
}

/* ---- API: GET /api/calculate/posix  ----- */
static int
CalcPosixHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int count = 0;
    double *scores = db_get_scores_array(global_db, &count);
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404, "No scores in database. POST /api/seed first.");
    }

    calc_result_t r = run_posix(scores, count);
    free(scores);

    char data[2048];
    format_result_json(data, sizeof(data), &r, "posix");

    return SendJSONResponse(conn, "success", "POSIX (pthreads) calculation completed", data);
}

/* ---- API: GET /api/calculate/compare  ----- */
static int
CalcCompareHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    if (strcmp(ri->request_method, "GET") != 0)
        return SendErrorResponse(conn, 405, "Only GET method supported");
    if (!global_db)
        return SendErrorResponse(conn, 500, "Database connection not available");

    int count = 0;
    double *scores = db_get_scores_array(global_db, &count);
    if (!scores || count == 0) {
        if (scores) free(scores);
        return SendErrorResponse(conn, 404, "No scores in database. POST /api/seed first.");
    }

    /* Run serial (force 1 thread) */
    int prev = omp_get_max_threads();
    omp_set_num_threads(1);
    calc_result_t serial   = run_serial(scores, count);
    omp_set_num_threads(prev);

    /* Run parallel */
    calc_result_t parallel = run_parallel(scores, count);

    free(scores);

    double speedup = (parallel.elapsed_ms > 0)
        ? serial.elapsed_ms / parallel.elapsed_ms : 0.0;

    char ser_json[2048], par_json[2048];
    format_result_json(ser_json, sizeof(ser_json), &serial, "serial");
    format_result_json(par_json, sizeof(par_json), &parallel, "parallel");

    /* Build big response */
    char *data = (char*)malloc(8192);
    snprintf(data, 8192,
        "{\n"
        "    \"serial\": %s,\n"
        "    \"parallel\": %s,\n"
        "    \"comparison\": {\n"
        "      \"serial_time_ms\": %.4f,\n"
        "      \"parallel_time_ms\": %.4f,\n"
        "      \"speedup\": %.4f,\n"
        "      \"serial_threads\": %d,\n"
        "      \"parallel_threads\": %d,\n"
        "      \"data_size\": %d,\n"
        "      \"improvement_pct\": %.2f\n"
        "    }\n"
        "  }",
        ser_json, par_json,
        serial.elapsed_ms, parallel.elapsed_ms, speedup,
        serial.threads_used, parallel.threads_used,
        count,
        (speedup > 1.0) ? (speedup - 1.0) * 100.0 : 0.0);

    int result = SendJSONResponse(conn, "success",
        "Serial vs Parallel comparison completed", data);
    free(data);
    return result;
}

// Exit Handler
static int
ExitHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    printf("Shutdown requested\n");
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: "
              "text/plain\r\nConnection: close\r\n\r\n");
    mg_printf(conn, "Server shutdown initiated.\n");
    mg_printf(conn, "Total requests handled: %u\n", requestCounter);
    mg_printf(conn, "Goodbye!\n");
    exitNow = 1;
    return 1;
}

// Main API Router
static int
ApiHandler(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *url = ri->local_uri;
    
    // Route to appropriate handler based on URL
    if (strncmp(url, "/api/students", 13) == 0) {
        return UsersHandler(conn, cbdata);
    }
    else if (strncmp(url, "/api/data", 9) == 0) {
        return DataHandler(conn, cbdata);
    }
    else if (strncmp(url, "/api/test", 9) == 0) {
        return TestHandler(conn, cbdata);
    }
    else if (strcmp(url, "/api/seed") == 0) {
        return SeedHandler(conn, cbdata);
    }
    else if (strcmp(url, "/api/calculate/serial") == 0) {
        return CalcSerialHandler(conn, cbdata);
    }
    else if (strcmp(url, "/api/calculate/parallel") == 0) {
        return CalcParallelHandler(conn, cbdata);
    }
    else if (strcmp(url, "/api/calculate/posix") == 0) {
        return CalcPosixHandler(conn, cbdata);
    }
    else if (strcmp(url, "/api/calculate/compare") == 0) {
        return CalcCompareHandler(conn, cbdata);
    }
    
    return SendErrorResponse(conn, 404, "API endpoint not found");
}

static int
log_message(const struct mg_connection *conn, const char *message)
{
    (void)conn; /* Suppress unused parameter warning */
    printf("[CivetWeb] %s", message);
    return 1;
}

int
main(int argc, char *argv[])
{
    (void)argc; /* Suppress unused parameter warning */
    (void)argv; /* Suppress unused parameter warning */
    const char *options[] = {
        "listening_ports", PORT,
        "request_timeout_ms", "10000",
        "error_log_file", "score_analyzer_error.log",
        "access_log_file", "score_analyzer_access.log",
        0
    };

    struct mg_callbacks callbacks;
    struct mg_context *ctx;
    config_t *config = NULL;

    /* Init libcivetweb */
    mg_init_library(0);

    /* Callback will print error messages to console */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = log_message;

    /* Initialize configuration */
    config = config_load("config.env");
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        mg_exit_library();
        return EXIT_FAILURE;
    }
    
    /* Initialize database connection */
    global_db = db_init(config->mongodb_uri, config->db_name);
    if (!global_db) {
        fprintf(stderr, "Failed to initialize database connection\n");
        config_free(config);
        mg_exit_library();
        return EXIT_FAILURE;
    }
    
    /* Test database connection */
    if (!db_test_connection(global_db)) {
        fprintf(stderr, "Database connection test failed\n");
        db_cleanup(global_db);
        config_free(config);
        mg_exit_library();
        return EXIT_FAILURE;
    }
    
    /* Start CivetWeb web server */
    ctx = mg_start(&callbacks, 0, options);

    /* Check return value */
    if (ctx == NULL) {
        fprintf(stderr, "Cannot start CivetWeb - mg_start failed.\n");
        return EXIT_FAILURE;
    }

    /* Register handlers - order matters, more specific routes first */
    mg_set_request_handler(ctx, SEED_URI, SeedHandler, 0);
    mg_set_request_handler(ctx, CALC_SERIAL_URI, CalcSerialHandler, 0);
    mg_set_request_handler(ctx, CALC_PARALLEL_URI, CalcParallelHandler, 0);
    mg_set_request_handler(ctx, CALC_POSIX_URI, CalcPosixHandler, 0);
    mg_set_request_handler(ctx, CALC_COMPARE_URI, CalcCompareHandler, 0);
    mg_set_request_handler(ctx, HEALTH_URI, HealthHandler, 0);
    mg_set_request_handler(ctx, API_URI, ApiHandler, 0);
    mg_set_request_handler(ctx, EXIT_URI, ExitHandler, 0);
    mg_set_request_handler(ctx, ROOT_URI, RootHandler, 0);

    /* Show startup info */
    printf("\n=== Students Score Management Engine ===\n");
    printf("Server started on port %s\n", PORT);
    printf("\nAvailable endpoints:\n");
    printf("  Welcome:         %s\n", HOST_INFO);
    printf("  Health Check:    %s%s\n", HOST_INFO, HEALTH_URI);
    printf("  Students API:    %s%s (GET, POST, PUT, DELETE)\n", HOST_INFO, USERS_URI);
    printf("  Student by ID:   %s/api/students/{id} (GET, PUT, DELETE)\n", HOST_INFO);
    printf("  Data API:        %s%s (GET, POST)\n", HOST_INFO, DATA_URI);
    printf("  Test Endpoints:  %s/api/test/{type}\n", HOST_INFO);
    printf("    - performance: %s/api/test/performance\n", HOST_INFO);
    printf("    - error:       %s/api/test/error\n", HOST_INFO);
    printf("    - delay:       %s/api/test/delay\n", HOST_INFO);
    printf("    - large:       %s/api/test/large-response\n", HOST_INFO);
    printf("  Seed Data:       %s%s (POST)\n", HOST_INFO, SEED_URI);
    printf("  Serial Calc:     %s%s (GET)\n", HOST_INFO, CALC_SERIAL_URI);
    printf("  Parallel Calc:   %s%s (GET)\n", HOST_INFO, CALC_PARALLEL_URI);
    printf("  POSIX Calc:      %s%s (GET)\n", HOST_INFO, CALC_POSIX_URI);
    printf("  Compare:         %s%s (GET)\n", HOST_INFO, CALC_COMPARE_URI);
    printf("  Shutdown:        %s%s\n", HOST_INFO, EXIT_URI);
    printf("\n  OpenMP threads available: %d\n", omp_get_max_threads());
    printf("\nPress Ctrl+C or visit %s%s to stop the server\n", HOST_INFO, EXIT_URI);
    printf("===============================\n\n");

    /* Wait until the server should be closed */
    while (!exitNow) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    /* Stop the server */
    mg_stop(ctx);
    
    /* Cleanup database connection */
    if (global_db) {
        db_cleanup(global_db);
        global_db = NULL;
    }
    
    /* Free configuration */
    if (config) {
        config_free(config);
    }
    
    /* Cleanup CivetWeb library */
    mg_exit_library();

    printf("\nServer stopped after handling %u requests.\n", requestCounter);
    printf("Logs saved to score_analyzer_*.log\n");
    printf("Goodbye!\n");

    return EXIT_SUCCESS;
}