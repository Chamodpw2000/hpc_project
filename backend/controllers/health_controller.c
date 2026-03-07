/*
 * Health Controller Implementation
 * Copyright (c) 2026
 * MIT License
 */

#include "health_controller.h"
#include "response_helper.h"

#include <stdio.h>
#include <string.h>

/* These are defined in score_analyzer_backend.c */
#define PORT      "8090"
#define HOST_INFO "http://localhost:8090"

/* Defined in civetweb.h / linked from civetweb.c */
#ifndef CIVETWEB_VERSION
#define CIVETWEB_VERSION "unknown"
#endif

int RootHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
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
            "      \"health\":   \"%s/health\",\n"
            "      \"api\":      \"%s/api\",\n"
            "      \"students\": \"%s/api/students\",\n"
            "      \"data\":     \"%s/api/data\"\n"
            "    }\n"
            "  }",
            CIVETWEB_VERSION, PORT,
            HOST_INFO, HOST_INFO, HOST_INFO, HOST_INFO);

        return SendJSONResponse(conn, "running",
            "Welcome to Students Score Management Engine"
            " - Your server is running on port " PORT,
            welcome_data);
    }

    return SendErrorResponse(conn, 405, "Only GET method supported for root endpoint");
}

int HealthHandler(struct mg_connection *conn, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(conn);

    printf("Health check request from %s\n", ri->remote_addr);

    if (0 == strcmp(ri->request_method, "GET")) {
        char health_data[1024];
        snprintf(health_data, sizeof(health_data),
            "{\n"
            "    \"server\": \"Score Analyzer Backend\",\n"
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
