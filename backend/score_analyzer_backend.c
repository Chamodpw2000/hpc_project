/*
 * Score Analyzer Backend – Entry Point
 * Registers routes and runs the CivetWeb server.
 * Copyright (c) 2026
 * MIT License
 */

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <omp.h>

#include "include/civetweb.h"
#include "include/config.h"
#include "include/db.h"

/* Controllers */
#include "controllers/response_helper.h"
#include "controllers/health_controller.h"
#include "controllers/student_controller.h"
#include "controllers/data_controller.h"
#include "controllers/score_controller.h"
#include "controllers/class_controller.h"

/* ---- Server constants -------------------------------------------------- */
#define PORT      "8090"
#define HOST_INFO "http://localhost:8090"

/* ---- API endpoint URIs ------------------------------------------------- */
#define ROOT_URI          "/"
#define HEALTH_URI        "/health"
#define API_URI           "/api/*"
#define USERS_URI         "/api/students"
#define DATA_URI          "/api/data"
#define EXIT_URI          "/exit"
#define SEED_URI          "/api/seed"
#define CALC_SERIAL_URI   "/api/calculate/serial"
#define CALC_PARALLEL_URI "/api/calculate/parallel"
#define CALC_COMPARE_URI  "/api/calculate/compare"
#define CLASSES_URI       "/api/classes"
#define SUBJECTS_URI      "/api/subjects"

/* ---- Shared globals ---------------------------------------------------- */
int              exitNow   = 0;
db_connection_t *global_db = NULL;
/* requestCounter is defined in controllers/response_helper.c */

/* ---- Shutdown handler -------------------------------------------------- */
static int ExitHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    printf("Shutdown requested\n");
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n"
              "Connection: close\r\n\r\n");
    mg_printf(conn, "Server shutdown initiated.\n");
    mg_printf(conn, "Total requests handled: %u\n", requestCounter);
    mg_printf(conn, "Goodbye!\n");
    exitNow = 1;
    return 1;
}

/* ---- Catch-all API router --------------------------------------------- */
static int ApiHandler(struct mg_connection *conn, void *cbdata)
{
    const struct mg_request_info *ri  = mg_get_request_info(conn);
    const char                   *url = ri->local_uri;

    if (strncmp(url, "/api/students", 13) == 0) return UsersHandler(conn, cbdata);
    if (strncmp(url, "/api/classes",  12) == 0) return ClassHandler(conn, cbdata);
    if (strncmp(url, "/api/subjects", 13) == 0) return SubjectHandler(conn, cbdata);
    if (strncmp(url, "/api/data",      9) == 0) return DataHandler(conn, cbdata);
    if (strncmp(url, "/api/test",      9) == 0) return TestHandler(conn, cbdata);
    if (strcmp (url, "/api/seed")         == 0) return SeedHandler(conn, cbdata);
    if (strcmp (url, CALC_SERIAL_URI)     == 0) return CalcSerialHandler(conn, cbdata);
    if (strcmp (url, CALC_PARALLEL_URI)   == 0) return CalcParallelHandler(conn, cbdata);
    if (strcmp (url, CALC_COMPARE_URI)    == 0) return CalcCompareHandler(conn, cbdata);

    return SendErrorResponse(conn, 404, "API endpoint not found");
}

/* ---- CivetWeb log callback -------------------------------------------- */
static int log_message(const struct mg_connection *conn, const char *message)
{
    (void)conn;
    printf("[CivetWeb] %s", message);
    return 1;
}

/* ======================================================================== */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    const char *options[] = {
        "listening_ports",    PORT,
        "request_timeout_ms", "10000",
        "error_log_file",     "score_analyzer_error.log",
        "access_log_file",    "score_analyzer_access.log",
        NULL
    };

    struct mg_callbacks callbacks;
    struct mg_context   *ctx;
    config_t            *config = NULL;

    mg_init_library(0);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = log_message;

    /* Load config */
    config = config_load("config.env");
    if (!config) {
        fprintf(stderr, "Failed to load configuration\n");
        mg_exit_library();
        return EXIT_FAILURE;
    }

    /* Connect to MongoDB */
    global_db = db_init(config->mongodb_uri, config->db_name);
    if (!global_db) {
        fprintf(stderr, "Failed to initialize database connection\n");
        config_free(config);
        mg_exit_library();
        return EXIT_FAILURE;
    }

    if (!db_test_connection(global_db)) {
        fprintf(stderr, "Database connection test failed\n");
        db_cleanup(global_db);
        config_free(config);
        mg_exit_library();
        return EXIT_FAILURE;
    }

    /* Start web server */
    ctx = mg_start(&callbacks, 0, options);
    if (!ctx) {
        fprintf(stderr, "Cannot start CivetWeb - mg_start failed.\n");
        db_cleanup(global_db);
        config_free(config);
        mg_exit_library();
        return EXIT_FAILURE;
    }

    /* Register handlers – specific routes first, wildcards last */
    mg_set_request_handler(ctx, SEED_URI,            SeedHandler,         0);
    mg_set_request_handler(ctx, CALC_SERIAL_URI,     CalcSerialHandler,   0);
    mg_set_request_handler(ctx, CALC_PARALLEL_URI,   CalcParallelHandler, 0);
    mg_set_request_handler(ctx, CALC_COMPARE_URI,    CalcCompareHandler,  0);
    mg_set_request_handler(ctx, HEALTH_URI,          HealthHandler,       0);
    /* Classes & Subjects need explicit + wildcard registration so both
       /api/classes  and  /api/classes/{name}  are matched */
    mg_set_request_handler(ctx, "/api/classes",      ClassHandler,        0);
    mg_set_request_handler(ctx, "/api/classes/*",    ClassHandler,        0);
    mg_set_request_handler(ctx, "/api/subjects",     SubjectHandler,      0);
    mg_set_request_handler(ctx, "/api/subjects/*",   SubjectHandler,      0);
    mg_set_request_handler(ctx, API_URI,             ApiHandler,          0);
    mg_set_request_handler(ctx, EXIT_URI,            ExitHandler,         0);
    mg_set_request_handler(ctx, ROOT_URI,            RootHandler,         0);

    /* Startup banner */
    printf("\n=== Students Score Management Engine ===\n");
    printf("Server started on port %s\n", PORT);
    printf("\nAvailable endpoints:\n");
    printf("  Welcome:         %s\n",                    HOST_INFO);
    printf("  Health Check:    %s%s\n",                  HOST_INFO, HEALTH_URI);
    printf("  Students API:    %s%s  (GET POST PUT DELETE)\n", HOST_INFO, USERS_URI);
    printf("  Student by ID:   %s/api/students/{id}\n",  HOST_INFO);
    printf("  Classes API:     %s%s  (GET POST DELETE)\n", HOST_INFO, CLASSES_URI);
    printf("  Subjects API:    %s%s  (GET POST DELETE)\n", HOST_INFO, SUBJECTS_URI);
    printf("  Data API:        %s%s  (GET POST)\n",      HOST_INFO, DATA_URI);
    printf("  Test Endpoints:  %s/api/test/{type}\n",    HOST_INFO);
    printf("  Seed Data:       %s%s (POST)\n",           HOST_INFO, SEED_URI);
    printf("  Serial Calc:     %s%s (GET)\n",            HOST_INFO, CALC_SERIAL_URI);
    printf("  Parallel Calc:   %s%s (GET)\n",            HOST_INFO, CALC_PARALLEL_URI);
    printf("  Compare:         %s%s (GET)\n",            HOST_INFO, CALC_COMPARE_URI);
    printf("  Shutdown:        %s%s\n",                  HOST_INFO, EXIT_URI);
    printf("\n  OpenMP threads available: %d\n",         omp_get_max_threads());
    printf("\nPress Ctrl+C or visit %s%s to stop\n",     HOST_INFO, EXIT_URI);
    printf("==========================================\n\n");

    /* Server loop */
    while (!exitNow) {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    /* Cleanup */
    mg_stop(ctx);
    if (global_db) { db_cleanup(global_db); global_db = NULL; }
    if (config)    { config_free(config); }
    mg_exit_library();

    printf("\nServer stopped after handling %u requests.\n", requestCounter);
    printf("Logs saved to score_analyzer_*.log\n");
    printf("Goodbye!\n");

    return EXIT_SUCCESS;
}