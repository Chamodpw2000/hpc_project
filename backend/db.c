/*
 * MongoDB Database Implementation for Score Analyzer Backend
 * Copyright (c) 2026
 * MIT License
 */

#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>

/* Initialize MongoDB connection */
db_connection_t* db_init(const char *connection_string, const char *db_name)
{
    db_connection_t *db = (db_connection_t*)malloc(sizeof(db_connection_t));
    if (!db) {
        fprintf(stderr, "Failed to allocate memory for database connection\n");
        return NULL;
    }
    
    memset(db, 0, sizeof(db_connection_t));
    
    /* Initialize MongoDB C Driver */
    mongoc_init();
    
    /* Create MongoDB client */
    db->client = mongoc_client_new(connection_string);
    if (!db->client) {
        fprintf(stderr, "Failed to create MongoDB client\n");
        mongoc_cleanup();
        free(db);
        return NULL;
    }
    
    /* Get database */
    db->database = mongoc_client_get_database(db->client, db_name);
    
    /* Get collections */
    db->students_collection = mongoc_client_get_collection(db->client, db_name, "students");
    db->scores_collection = mongoc_client_get_collection(db->client, db_name, "scores");
    
    db->connection_string = strdup(connection_string);
    db->connected = 1;
    
    printf("✓ MongoDB connection initialized\n");
    printf("  Database: %s\n", db_name);
    
    return db;
}

/* Clean up database connection */
void db_cleanup(db_connection_t *db)
{
    if (!db) return;
    
    if (db->students_collection) {
        mongoc_collection_destroy(db->students_collection);
    }
    if (db->scores_collection) {
        mongoc_collection_destroy(db->scores_collection);
    }
    if (db->database) {
        mongoc_database_destroy(db->database);
    }
    if (db->client) {
        mongoc_client_destroy(db->client);
    }
    if (db->connection_string) {
        free(db->connection_string);
    }
    
    mongoc_cleanup();
    free(db);
    
    printf("✓ MongoDB connection closed\n");
}

/* Test database connection */
int db_test_connection(db_connection_t *db)
{
    if (!db || !db->client) {
        return 0;
    }
    
    bson_error_t error;
    bson_t *command = BCON_NEW("ping", BCON_INT32(1));
    bson_t reply;
    
    bool success = mongoc_client_command_simple(
        db->client, "admin", command, NULL, &reply, &error);
    
    if (!success) {
        fprintf(stderr, "MongoDB ping failed: %s\n", error.message);
        bson_destroy(command);
        bson_destroy(&reply);
        return 0;
    }
    
    bson_destroy(command);
    bson_destroy(&reply);
    
    printf("✓ MongoDB connection test successful\n");
    return 1;
}

/* Create a new student */
int db_create_student(db_connection_t *db, const char *name, const char *email, const char *student_id)
{
    if (!db || !db->students_collection) {
        return 0;
    }
    
    bson_error_t error;
    bson_t *doc = bson_new();
    
    BSON_APPEND_UTF8(doc, "student_id", student_id);
    BSON_APPEND_UTF8(doc, "name", name);
    BSON_APPEND_UTF8(doc, "email", email);
    BSON_APPEND_DATE_TIME(doc, "created_at", bson_get_monotonic_time());
    
    bool success = mongoc_collection_insert_one(
        db->students_collection, doc, NULL, NULL, &error);
    
    bson_destroy(doc);
    
    if (!success) {
        fprintf(stderr, "Failed to insert student: %s\n", error.message);
        return 0;
    }
    
    return 1;
}

/* Get all students as JSON string */
char* db_get_all_students(db_connection_t *db)
{
    if (!db || !db->students_collection) {
        return strdup("[]");
    }
    
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->students_collection, query, NULL, NULL);
    
    /* Build JSON array */
    char *result = (char*)malloc(65536);  /* 64KB buffer */
    strcpy(result, "[");
    
    const bson_t *doc;
    int first = 1;
    
    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) strcat(result, ",");
        strcat(result, str);
        bson_free(str);
        first = 0;
    }
    
    strcat(result, "]");
    
    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    
    return result;
}

/* Get student by ID */
char* db_get_student_by_id(db_connection_t *db, const char *student_id)
{
    if (!db || !db->students_collection) {
        return strdup("null");
    }
    
    bson_t *query = BCON_NEW("student_id", student_id);
    
    const bson_t *doc;
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->students_collection, query, NULL, NULL);
    
    char *result = NULL;
    if (mongoc_cursor_next(cursor, &doc)) {
        result = bson_as_canonical_extended_json(doc, NULL);
    } else {
        result = strdup("null");
    }
    
    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    
    return result;
}

/* Update student */
int db_update_student(db_connection_t *db, const char *student_id, const char *name, const char *email)
{
    if (!db || !db->students_collection) {
        return 0;
    }
    
    bson_error_t error;
    bson_t *query = BCON_NEW("student_id", student_id);
    bson_t *update = BCON_NEW("$set", "{",
        "name", BCON_UTF8(name),
        "email", BCON_UTF8(email),
        "updated_at", BCON_DATE_TIME(bson_get_monotonic_time()),
        "}");
    
    bool success = mongoc_collection_update_one(
        db->students_collection, query, update, NULL, NULL, &error);
    
    bson_destroy(query);
    bson_destroy(update);
    
    if (!success) {
        fprintf(stderr, "Failed to update student: %s\n", error.message);
        return 0;
    }
    
    return 1;
}

/* Delete student */
int db_delete_student(db_connection_t *db, const char *student_id)
{
    if (!db || !db->students_collection) {
        return 0;
    }
    
    bson_error_t error;
    bson_t *query = BCON_NEW("student_id", student_id);
    
    bool success = mongoc_collection_delete_one(
        db->students_collection, query, NULL, NULL, &error);
    
    bson_destroy(query);
    
    if (!success) {
        fprintf(stderr, "Failed to delete student: %s\n", error.message);
        return 0;
    }
    
    return 1;
}

/* Add score for a student */
int db_add_score(db_connection_t *db, const char *student_id, const char *subject, double score)
{
    if (!db || !db->scores_collection) {
        return 0;
    }
    
    bson_error_t error;
    bson_t *doc = bson_new();
    
    BSON_APPEND_UTF8(doc, "student_id", student_id);
    BSON_APPEND_UTF8(doc, "subject", subject);
    BSON_APPEND_DOUBLE(doc, "score", score);
    BSON_APPEND_DATE_TIME(doc, "created_at", bson_get_monotonic_time());
    
    bool success = mongoc_collection_insert_one(
        db->scores_collection, doc, NULL, NULL, &error);
    
    bson_destroy(doc);
    
    if (!success) {
        fprintf(stderr, "Failed to insert score: %s\n", error.message);
        return 0;
    }
    
    return 1;
}

/* Get scores for a student */
char* db_get_student_scores(db_connection_t *db, const char *student_id)
{
    if (!db || !db->scores_collection) {
        return strdup("[]");
    }
    
    bson_t *query = BCON_NEW("student_id", student_id);
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->scores_collection, query, NULL, NULL);
    
    char *result = (char*)malloc(65536);
    strcpy(result, "[");
    
    const bson_t *doc;
    int first = 1;
    
    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) strcat(result, ",");
        strcat(result, str);
        bson_free(str);
        first = 0;
    }
    
    strcat(result, "]");
    
    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    
    return result;
}

/* Get all scores */
char* db_get_all_scores(db_connection_t *db)
{
    if (!db || !db->scores_collection) {
        return strdup("[]");
    }
    
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->scores_collection, query, NULL, NULL);
    
    char *result = (char*)malloc(65536);
    strcpy(result, "[");
    
    const bson_t *doc;
    int first = 1;
    
    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) strcat(result, ",");
        strcat(result, str);
        bson_free(str);
        first = 0;
    }
    
    strcat(result, "]");
    
    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    
    return result;
}
