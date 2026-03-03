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

#include "civetweb.h"
#include "config.h"
#include "db.h"

#define PORT "8090"
#define HOST_INFO "http://localhost:8090"

// API Endpoints
#define ROOT_URI "/"
#define HEALTH_URI "/health"
#define API_URI "/api/*"
#define USERS_URI "/api/users"
#define USERS_ID_URI "/api/users/*"
#define DATA_URI "/api/data"
#define TEST_URI "/api/test/*"
#define EXIT_URI "/exit"

int exitNow = 0;
static unsigned requestCounter = 0;
static db_connection_t *global_db = NULL;

// Simple JSON response structure (without external cJSON dependency)
static int
SendJSONResponse(struct mg_connection *conn, const char *status, const char *message, const char *data)
{
    char response[2048];
    time_t now = time(NULL);
    
    snprintf(response, sizeof(response),
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
    
    /* Send HTTP message header */
    mg_send_http_ok(conn, "application/json; charset=utf-8", response_len);
    
    /* Send HTTP message content */
    mg_write(conn, response, response_len);
    
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
            "      \"users\": \"%s/api/users\",\n"
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

// Users API Handler - MongoDB integrated user management
static int
UsersHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata; /* Suppress unused parameter warning */
    const struct mg_request_info *ri = mg_get_request_info(conn);
    const char *url = ri->local_uri;
    char userid[256] = "";
    
    printf("Users API: %s %s\n", ri->request_method, url);
    
    // Check database connection
    if (!global_db) {
        return SendErrorResponse(conn, 500, "Database connection not available");
    }
    
    // Extract user ID if present (/api/users/123)
    if (strncmp(url, "/api/users/", 11) == 0) {
        const char *id_start = url + 11;
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
            // GET all users
            char *users_json = db_get_all_students(global_db);
            int result = SendJSONResponse(conn, "success", "Users retrieved successfully", users_json);
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
    
    return SendErrorResponse(conn, 405, "Method not supported for users endpoint");
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
    if (strncmp(url, "/api/users", 10) == 0) {
        return UsersHandler(conn, cbdata);
    }
    else if (strncmp(url, "/api/data", 9) == 0) {
        return DataHandler(conn, cbdata);
    }
    else if (strncmp(url, "/api/test", 9) == 0) {
        return TestHandler(conn, cbdata);
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
    printf("  Users API:       %s%s (GET, POST, PUT, DELETE)\n", HOST_INFO, USERS_URI);
    printf("  Users by ID:     %s/api/users/{id} (GET, PUT, DELETE)\n", HOST_INFO);
    printf("  Data API:        %s%s (GET, POST)\n", HOST_INFO, DATA_URI);
    printf("  Test Endpoints:  %s/api/test/{type}\n", HOST_INFO);
    printf("    - performance: %s/api/test/performance\n", HOST_INFO);
    printf("    - error:       %s/api/test/error\n", HOST_INFO);
    printf("    - delay:       %s/api/test/delay\n", HOST_INFO);
    printf("    - large:       %s/api/test/large-response\n", HOST_INFO);
    printf("  Shutdown:        %s%s\n", HOST_INFO, EXIT_URI);
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