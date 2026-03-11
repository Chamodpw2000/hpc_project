/*
 * Data & Test Controller Implementation
 * Copyright (c) 2026
 * MIT License
 */

#include "data_controller.h"
#include "response_helper.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- GET/POST /api/data ------------------------------------------------ */
int DataHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    printf("Data API: %s %s\n", ri->request_method, ri->local_uri);

    if (0 == strcmp(ri->request_method, "GET")) {
        const char *sample_data =
            "{\n"
            "    \"items\": [\n"
            "      {\"id\": 1, \"value\": \"apple\",  \"category\": \"fruit\"},\n"
            "      {\"id\": 2, \"value\": \"carrot\", \"category\": \"vegetable\"},\n"
            "      {\"id\": 3, \"value\": \"banana\", \"category\": \"fruit\"}\n"
            "    ],\n"
            "    \"total\": 3\n"
            "  }";
        return SendJSONResponse(conn, "success", "Data retrieved successfully", sample_data);
    }

    if (0 == strcmp(ri->request_method, "POST")) {
        char buffer[1024];
        int  dlen = mg_read(conn, buffer, sizeof(buffer) - 1);
        if (dlen > 0) {
            buffer[dlen] = '\0';
            printf("Received data: %s\n", buffer);
        }
        return SendJSONResponse(conn, "success", "Data processed successfully", "null");
    }

    return SendErrorResponse(conn, 405,
        "Only GET and POST methods supported for data endpoint");
}

/* ---- GET /api/test/{type} ---------------------------------------------- */
int TestHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri  = mg_get_request_info(conn);
    const char                   *url = ri->local_uri;
    char test_type[256] = "";

    /* Extract test type from URL: /api/test/{type} */
    if (strncmp(url, "/api/test/", 10) == 0) {
        const char *type_start = url + 10;
        if (*type_start != '\0') {
            strncpy(test_type, type_start, sizeof(test_type) - 1);
            test_type[sizeof(test_type) - 1] = '\0';
        }
    }

    printf("Test API: %s %s (type: %s)\n", ri->request_method, url, test_type);

    /* No type – list available tests */
    if (strlen(test_type) == 0) {
        const char *available =
            "[\n"
            "    \"performance\",\n"
            "    \"error\",\n"
            "    \"delay\",\n"
            "    \"large-response\"\n"
            "  ]";
        return SendJSONResponse(conn, "success", "Available test endpoints", available);
    }

    /* --- error --- */
    if (strcmp(test_type, "error") == 0)
        return SendErrorResponse(conn, 500, "This is a test error response");

    /* --- delay --- */
    if (strcmp(test_type, "delay") == 0) {
        printf("Simulating delay...\n");
#ifdef _WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
        return SendJSONResponse(conn, "success", "Delayed response completed", "null");
    }

    /* --- large-response --- */
    if (strcmp(test_type, "large-response") == 0) {
        char large_data[4096];
        snprintf(large_data, sizeof(large_data),
            "{\n"
            "    \"description\": \"Large test response\",\n"
            "    \"data\": \"%s\",\n"
            "    \"repeat_count\": 100\n"
            "  }",
            "This is a test string repeated to create a larger response payload.");
        return SendJSONResponse(conn, "success", "Large response generated", large_data);
    }

    /* --- performance --- */
    if (strcmp(test_type, "performance") == 0) {
        clock_t      start    = clock();
        volatile int dummy    = 0;
        for (int i = 0; i < 100000; i++) dummy += i;
        clock_t  end      = clock();
        double   cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;

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
