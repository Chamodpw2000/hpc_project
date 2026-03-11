/*
 * HTTP Response Helper Implementation
 * Copyright (c) 2026
 * MIT License
 */

#include "response_helper.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Shared request counter – defined (and incremented) here */
unsigned requestCounter = 0;

int SendJSONResponse(struct mg_connection *conn,
                     const char *status,
                     const char *message,
                     const char *data)
{
    size_t data_len  = data ? strlen(data) : 0;
    size_t buf_size  = 512 + data_len;
    char  *response  = (char *)malloc(buf_size);
    if (!response) return 500;

    time_t now = time(NULL);

    snprintf(response, buf_size,
        "{\n"
        "  \"status\": \"%s\",\n"
        "  \"message\": \"%s\",\n"
        "  \"timestamp\": %ld,\n"
        "  \"request_count\": %u%s%s\n"
        "}",
        status, message, (long)now, ++requestCounter,
        data ? ",\n  \"data\": " : "",
        data ? data : "");

    size_t response_len = strlen(response);

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Connection: close\r\n\r\n",
              response_len);

    mg_write(conn, response, response_len);
    free(response);
    return 200;
}

int SendErrorResponse(struct mg_connection *conn,
                      int status_code,
                      const char *message)
{
    char   response[1024];
    time_t now = time(NULL);

    snprintf(response, sizeof(response),
        "{\n"
        "  \"status\": \"error\",\n"
        "  \"message\": \"%s\",\n"
        "  \"timestamp\": %ld,\n"
        "  \"error_code\": %d\n"
        "}",
        message, (long)now, status_code);

    size_t response_len = strlen(response);

    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %zu\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Connection: close\r\n\r\n",
              status_code,
              (status_code == 404) ? "Not Found"             :
              (status_code == 405) ? "Method Not Allowed"    :
              (status_code == 400) ? "Bad Request"           :
              (status_code == 500) ? "Internal Server Error" : "Error",
              response_len);

    mg_write(conn, response, response_len);
    return status_code;
}
