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
#include <math.h>
#include <time.h>
#include <pthread.h>

/*
 * mongoc_client_t is NOT thread-safe.  CivetWeb dispatches each request on a
 * different worker thread, but we share a single client across all db_*
 * functions.  This mutex serialises every database call so only one thread
 * touches the driver at a time – simple and safe.
 */
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
#define DB_LOCK()   pthread_mutex_lock(&db_mutex)
#define DB_UNLOCK() pthread_mutex_unlock(&db_mutex)

/* Forward declaration */
static int buf_append(char **buf, size_t *len, size_t *cap, const char *src);

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
    
    /* ── Build a URI with sensible timeout / retry defaults ── */
    bson_error_t uri_err;
    mongoc_uri_t *uri = mongoc_uri_new_with_error(connection_string, &uri_err);
    if (!uri) {
        fprintf(stderr, "Failed to parse MongoDB URI: %s\n", uri_err.message);
        mongoc_cleanup();
        free(db);
        return NULL;
    }

    /* Ensure timeouts are set so stale streams don't hang forever */
    if (mongoc_uri_get_option_as_int32(uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS, 0) == 0)
        mongoc_uri_set_option_as_int32(uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS, 5000);
    if (mongoc_uri_get_option_as_int32(uri, MONGOC_URI_CONNECTTIMEOUTMS, 0) == 0)
        mongoc_uri_set_option_as_int32(uri, MONGOC_URI_CONNECTTIMEOUTMS, 10000);
    if (mongoc_uri_get_option_as_int32(uri, MONGOC_URI_SOCKETTIMEOUTMS, 0) == 0)
        mongoc_uri_set_option_as_int32(uri, MONGOC_URI_SOCKETTIMEOUTMS, 30000);

    /* Enable retryable reads & writes (guards against transient cluster errors) */
    mongoc_uri_set_option_as_bool(uri, MONGOC_URI_RETRYREADS,  true);
    mongoc_uri_set_option_as_bool(uri, MONGOC_URI_RETRYWRITES, true);

    /* Create a thread-safe client pool */
    db->pool = mongoc_client_pool_new(uri);
    if (!db->pool) {
        fprintf(stderr, "Failed to create MongoDB client pool\n");
        mongoc_uri_destroy(uri);
        mongoc_cleanup();
        free(db);
        return NULL;
    }
    mongoc_client_pool_set_error_api(db->pool, MONGOC_ERROR_API_VERSION_2);
    mongoc_client_pool_set_appname(db->pool, "score-analyzer-backend");

    /* Also create a single client for backwards compatibility */
    db->client = mongoc_client_pool_pop(db->pool);
    mongoc_uri_destroy(uri);

    if (!db->client) {
        fprintf(stderr, "Failed to obtain MongoDB client from pool\n");
        mongoc_client_pool_destroy(db->pool);
        mongoc_cleanup();
        free(db);
        return NULL;
    }

    /* Get database */
    db->database = mongoc_client_get_database(db->client, db_name);
    
    /* Get collections – all four are created once and reused */
    db->students_collection = mongoc_client_get_collection(db->client, db_name, "students");
    db->scores_collection   = mongoc_client_get_collection(db->client, db_name, "scores");
    db->classes_collection  = mongoc_client_get_collection(db->client, db_name, "classes");
    db->subjects_collection = mongoc_client_get_collection(db->client, db_name, "subjects");
    
    db->connection_string = strdup(connection_string);
    strncpy(db->db_name, db_name, sizeof(db->db_name) - 1);
    db->db_name[sizeof(db->db_name) - 1] = '\0';
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
    if (db->classes_collection) {
        mongoc_collection_destroy(db->classes_collection);
    }
    if (db->subjects_collection) {
        mongoc_collection_destroy(db->subjects_collection);
    }
    if (db->database) {
        mongoc_database_destroy(db->database);
    }
    if (db->client && db->pool) {
        mongoc_client_pool_push(db->pool, db->client);
    } else if (db->client) {
        mongoc_client_destroy(db->client);
    }
    if (db->pool) {
        mongoc_client_pool_destroy(db->pool);
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

    DB_LOCK();
    bson_error_t error;
    bson_t *command = BCON_NEW("ping", BCON_INT32(1));
    bson_t reply;
    
    bool success = mongoc_client_command_simple(
        db->client, "admin", command, NULL, &reply, &error);
    
    if (!success) {
        fprintf(stderr, "MongoDB ping failed: %s\n", error.message);
        bson_destroy(command);
        bson_destroy(&reply);
        DB_UNLOCK();
        return 0;
    }
    
    bson_destroy(command);
    bson_destroy(&reply);
    DB_UNLOCK();
    
    printf("✓ MongoDB connection test successful\n");
    return 1;
}

/* Create a new student */
int db_create_student(db_connection_t *db, const char *name, const char *email, const char *student_id)
{
    if (!db || !db->students_collection) {
        return 0;
    }

    DB_LOCK();
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
        DB_UNLOCK();
        return 0;
    }

    DB_UNLOCK();
    return 1;
}

/* Create a new student with class assignment */
int db_create_student_with_class(db_connection_t *db, const char *name, const char *email,
                                  const char *student_id, const char *class_name)
{
    if (!db || !db->students_collection) {
        return 0;
    }

    DB_LOCK();
    bson_error_t error;
    bson_t *doc = bson_new();

    BSON_APPEND_UTF8(doc, "student_id", student_id);
    BSON_APPEND_UTF8(doc, "name", name);
    BSON_APPEND_UTF8(doc, "email", email);
    if (class_name && class_name[0] != '\0') {
        BSON_APPEND_UTF8(doc, "class_name", class_name);
    }
    BSON_APPEND_DATE_TIME(doc, "created_at", bson_get_monotonic_time());

    bool success = mongoc_collection_insert_one(
        db->students_collection, doc, NULL, NULL, &error);

    bson_destroy(doc);

    if (!success) {
        fprintf(stderr, "Failed to insert student: %s\n", error.message);
        DB_UNLOCK();
        return 0;
    }

    DB_UNLOCK();
    return 1;
}

/* Get students filtered by class as JSON array */
char* db_get_students_by_class(db_connection_t *db, const char *class_name)
{
    if (!db || !db->students_collection || !class_name) {
        return strdup("[]");
    }

    DB_LOCK();

    bson_t *query = BCON_NEW("class_name", class_name);
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->students_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char*)malloc(cap);
    if (!result) { mongoc_cursor_destroy(cursor); bson_destroy(query); DB_UNLOCK(); return strdup("[]"); }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;

    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) buf_append(&result, &len, &cap, ",");
        buf_append(&result, &len, &cap, str);
        bson_free(str);
        first = 0;
    }

    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);

    DB_UNLOCK();
    return result;
}

static int buf_append(char **buf, size_t *len, size_t *cap, const char *src)
{
    size_t src_len = strlen(src);
    if (*len + src_len + 2 > *cap) {
        size_t new_cap = (*cap + src_len + 2) * 2;
        char *tmp = (char*)realloc(*buf, new_cap);
        if (!tmp) return 0;
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, src_len);
    *len += src_len;
    (*buf)[*len] = '\0';
    return 1;
}

/* Get all students as JSON string */
char* db_get_all_students(db_connection_t *db)
{
    if (!db || !db->students_collection) {
        return strdup("[]");
    }

    DB_LOCK();
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->students_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char*)malloc(cap);
    if (!result) { mongoc_cursor_destroy(cursor); bson_destroy(query); DB_UNLOCK(); return strdup("[]"); }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;

    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) buf_append(&result, &len, &cap, ",");
        buf_append(&result, &len, &cap, str);
        bson_free(str);
        first = 0;
    }

    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);

    DB_UNLOCK();
    return result;
}

/* Get student by ID */
char* db_get_student_by_id(db_connection_t *db, const char *student_id)
{
    if (!db || !db->students_collection) {
        return strdup("null");
    }

    DB_LOCK();
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

    DB_UNLOCK();
    return result;
}

/* Update student */
int db_update_student(db_connection_t *db, const char *student_id, const char *name, const char *email)
{
    if (!db || !db->students_collection) {
        return 0;
    }

    DB_LOCK();
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
        DB_UNLOCK();
        return 0;
    }

    DB_UNLOCK();
    return 1;
}

/* Delete student */
int db_delete_student(db_connection_t *db, const char *student_id)
{
    if (!db || !db->students_collection) {
        return 0;
    }

    DB_LOCK();
    bson_error_t error;
    bson_t *query = BCON_NEW("student_id", student_id);

    bool success = mongoc_collection_delete_one(
        db->students_collection, query, NULL, NULL, &error);

    bson_destroy(query);

    if (!success) {
        fprintf(stderr, "Failed to delete student: %s\n", error.message);
        DB_UNLOCK();
        return 0;
    }

    DB_UNLOCK();
    return 1;
}

/* Add score for a student */
int db_add_score(db_connection_t *db, const char *student_id, const char *subject, double score)
{
    if (!db || !db->scores_collection) {
        return 0;
    }

    DB_LOCK();
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
        DB_UNLOCK();
        return 0;
    }

    DB_UNLOCK();
    return 1;
}

/* Update score for a student + subject (upsert: create if not found) */
int db_update_score(db_connection_t *db, const char *student_id, const char *subject, double score)
{
    if (!db || !db->scores_collection) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("student_id", student_id, "subject", subject);
    bson_t *update = BCON_NEW(
        "$set", "{",
            "score", BCON_DOUBLE(score),
            "updated_at", BCON_DATE_TIME(bson_get_monotonic_time()),
        "}",
        "$setOnInsert", "{",
            "student_id", BCON_UTF8(student_id),
            "subject",    BCON_UTF8(subject),
            "created_at", BCON_DATE_TIME(bson_get_monotonic_time()),
        "}"
    );
    bson_t *opts = BCON_NEW("upsert", BCON_BOOL(true));

    bool ok = mongoc_collection_update_one(
        db->scores_collection, filter, update, opts, NULL, &error);

    bson_destroy(filter);
    bson_destroy(update);
    bson_destroy(opts);

    if (!ok) {
        fprintf(stderr, "db_update_score: %s\n", error.message);
        DB_UNLOCK();
        return 0;
    }
    DB_UNLOCK();
    return 1;
}

/* Delete a score for a student + subject */
int db_delete_score(db_connection_t *db, const char *student_id, const char *subject)
{
    if (!db || !db->scores_collection) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("student_id", student_id, "subject", subject);

    bool ok = mongoc_collection_delete_one(
        db->scores_collection, filter, NULL, NULL, &error);

    bson_destroy(filter);

    if (!ok) {
        fprintf(stderr, "db_delete_score: %s\n", error.message);
        DB_UNLOCK();
        return 0;
    }
    DB_UNLOCK();
    return 1;
}

/* Get scores for a student */
char* db_get_student_scores(db_connection_t *db, const char *student_id)
{
    if (!db || !db->scores_collection) {
        return strdup("[]");
    }

    DB_LOCK();
    bson_t *query = BCON_NEW("student_id", student_id);
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->scores_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char*)malloc(cap);
    if (!result) { mongoc_cursor_destroy(cursor); bson_destroy(query); DB_UNLOCK(); return strdup("[]"); }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;

    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) buf_append(&result, &len, &cap, ",");
        buf_append(&result, &len, &cap, str);
        bson_free(str);
        first = 0;
    }

    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);

    DB_UNLOCK();
    return result;
}

/* Get all scores */
char* db_get_all_scores(db_connection_t *db)
{
    if (!db || !db->scores_collection) {
        return strdup("[]");
    }

    DB_LOCK();
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->scores_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char*)malloc(cap);
    if (!result) { mongoc_cursor_destroy(cursor); bson_destroy(query); DB_UNLOCK(); return strdup("[]"); }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;

    while (mongoc_cursor_next(cursor, &doc)) {
        char *str = bson_as_canonical_extended_json(doc, NULL);
        if (!first) buf_append(&result, &len, &cap, ",");
        buf_append(&result, &len, &cap, str);
        bson_free(str);
        first = 0;
    }

    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);

    DB_UNLOCK();
    return result;
}

/* Seed dummy student and score data for OpenMP testing */
int db_seed_dummy_data(db_connection_t *db, int num_students, int scores_per_student)
{
    if (!db || !db->students_collection || !db->scores_collection) {
        return 0;
    }

    DB_LOCK();
    const char *subjects[] = {
        "Mathematics", "Physics", "Chemistry", "Biology", "English",
        "Computer Science", "History", "Geography", "Economics", "Statistics"
    };
    int num_subjects = 10;

    srand((unsigned int)time(NULL));

    /* Clear existing data first */
    bson_t *empty_filter = bson_new();
    bson_error_t error;
    mongoc_collection_delete_many(db->students_collection, empty_filter, NULL, NULL, &error);
    mongoc_collection_delete_many(db->scores_collection, empty_filter, NULL, NULL, &error);
    bson_destroy(empty_filter);

    int total_scores = 0;

    for (int i = 0; i < num_students; i++) {
        char student_id[32], name[64], email[64];
        snprintf(student_id, sizeof(student_id), "STU%05d", i + 1);
        snprintf(name, sizeof(name), "Student_%d", i + 1);
        snprintf(email, sizeof(email), "student%d@university.edu", i + 1);

        /* Insert student */
        bson_t *student_doc = bson_new();
        BSON_APPEND_UTF8(student_doc, "student_id", student_id);
        BSON_APPEND_UTF8(student_doc, "name", name);
        BSON_APPEND_UTF8(student_doc, "email", email);
        BSON_APPEND_DATE_TIME(student_doc, "created_at", bson_get_monotonic_time());

        if (!mongoc_collection_insert_one(db->students_collection, student_doc, NULL, NULL, &error)) {
            fprintf(stderr, "Failed to insert student %s: %s\n", student_id, error.message);
        }
        bson_destroy(student_doc);

        /* Insert scores for this student */
        for (int j = 0; j < scores_per_student; j++) {
            const char *subject = subjects[j % num_subjects];
            /* Generate random score between 20.0 and 100.0 */
            double score = 20.0 + ((double)rand() / RAND_MAX) * 80.0;

            bson_t *score_doc = bson_new();
            BSON_APPEND_UTF8(score_doc, "student_id", student_id);
            BSON_APPEND_UTF8(score_doc, "subject", subject);
            BSON_APPEND_DOUBLE(score_doc, "score", score);
            BSON_APPEND_DATE_TIME(score_doc, "created_at", bson_get_monotonic_time());

            if (!mongoc_collection_insert_one(db->scores_collection, score_doc, NULL, NULL, &error)) {
                fprintf(stderr, "Failed to insert score: %s\n", error.message);
            } else {
                total_scores++;
            }
            bson_destroy(score_doc);
        }
    }

    DB_UNLOCK();
    printf("Seeded %d students with %d total scores\n", num_students, total_scores);
    return total_scores;
}

/* Fetch all score values as a raw double array */
double* db_get_scores_array(db_connection_t *db, int *out_count)
{
    *out_count = 0;
    if (!db || !db->scores_collection) {
        return NULL;
    }

    DB_LOCK();
    /* First count documents */
    bson_t *filter = bson_new();
    bson_error_t error;
    int64_t count = mongoc_collection_count_documents(
        db->scores_collection, filter, NULL, NULL, NULL, &error);

    if (count <= 0) {
        bson_destroy(filter);
        DB_UNLOCK();
        return NULL;
    }

    double *scores = (double*)malloc(sizeof(double) * (size_t)count);
    if (!scores) {
        bson_destroy(filter);
        DB_UNLOCK();
        return NULL;
    }

    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(
        db->scores_collection, filter, NULL, NULL);

    const bson_t *doc;
    int idx = 0;

    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t iter;
        if (bson_iter_init_find(&iter, doc, "score") && BSON_ITER_HOLDS_DOUBLE(&iter)) {
            scores[idx++] = bson_iter_double(&iter);
        }
    }

    mongoc_cursor_destroy(cursor);
    bson_destroy(filter);

    *out_count = idx;
    DB_UNLOCK();
    printf("Fetched %d scores from database\n", idx);
    return scores;
}

/* Get total count of score documents */
int db_get_scores_count(db_connection_t *db)
{
    if (!db || !db->scores_collection) return 0;

    DB_LOCK();
    bson_t *filter = bson_new();
    bson_error_t error;
    int64_t count = mongoc_collection_count_documents(
        db->scores_collection, filter, NULL, NULL, NULL, &error);
    bson_destroy(filter);
    DB_UNLOCK();

    return (int)count;
}

/* ============================================================
 * CLASS OPERATIONS  (collection: "classes")
 * ============================================================ */

/* Create a new class (unique by name) */
int db_create_class(db_connection_t *db, const char *name)
{
    if (!db || !db->classes_collection || !name) return 0;

    DB_LOCK();
    /* Upsert so duplicates are silently ignored */
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", name);
    bson_t *update = BCON_NEW("$setOnInsert", "{", "name", name, "}");
    bson_t  opts   = BSON_INITIALIZER;
    BSON_APPEND_BOOL(&opts, "upsert", true);

    bool ok = mongoc_collection_update_one(db->classes_collection, filter, update, &opts, NULL, &error);

    bson_destroy(filter);
    bson_destroy(update);
    bson_destroy(&opts);

    if (!ok) { fprintf(stderr, "db_create_class: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

/* Return all classes as a JSON array string (caller must free) */
char* db_get_all_classes(db_connection_t *db)
{
    if (!db || !db->classes_collection) return strdup("[]");

    DB_LOCK();
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(db->classes_collection, query, NULL, NULL);

    size_t cap = 2048, len = 0;
    char *result = (char *)malloc(cap);
    if (!result) {
        mongoc_cursor_destroy(cursor); bson_destroy(query);
        DB_UNLOCK();
        return strdup("[]");
    }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t iter;
        if (bson_iter_init_find(&iter, doc, "name") && BSON_ITER_HOLDS_UTF8(&iter)) {
            const char *name_val = bson_iter_utf8(&iter, NULL);
            char quoted[256];
            snprintf(quoted, sizeof(quoted), "\"%s\"", name_val);
            if (!first) buf_append(&result, &len, &cap, ",");
            buf_append(&result, &len, &cap, quoted);
            first = 0;
        }
    }
    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    DB_UNLOCK();
    return result;
}

/* Delete a class by name */
int db_delete_class(db_connection_t *db, const char *name)
{
    if (!db || !db->classes_collection || !name) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", name);
    bool ok = mongoc_collection_delete_one(db->classes_collection, filter, NULL, NULL, &error);
    bson_destroy(filter);

    if (!ok) { fprintf(stderr, "db_delete_class: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

/* Rename a class (update the name field) */
int db_rename_class(db_connection_t *db, const char *old_name, const char *new_name)
{
    if (!db || !db->classes_collection || !old_name || !new_name) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", old_name);
    bson_t *update = BCON_NEW("$set", "{", "name", new_name, "}");
    bool ok = mongoc_collection_update_one(db->classes_collection, filter, update, NULL, NULL, &error);
    bson_destroy(filter);
    bson_destroy(update);

    if (!ok) { fprintf(stderr, "db_rename_class: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

/* ============================================================
 * SUBJECT OPERATIONS  (collection: "subjects")
 * ============================================================ */

/* Create a subject (name + class_name, unique pair via upsert) */
int db_create_subject(db_connection_t *db, const char *name, const char *class_name)
{
    if (!db || !db->subjects_collection || !name || !class_name) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", name, "class_name", class_name);
    bson_t *update = BCON_NEW("$setOnInsert", "{",
                                  "name",       name,
                                  "class_name", class_name,
                              "}");
    bson_t opts = BSON_INITIALIZER;
    BSON_APPEND_BOOL(&opts, "upsert", true);

    bool ok = mongoc_collection_update_one(db->subjects_collection, filter, update, &opts, NULL, &error);

    bson_destroy(filter);
    bson_destroy(update);
    bson_destroy(&opts);

    if (!ok) { fprintf(stderr, "db_create_subject: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

/* Return all subjects as a JSON array of objects { name, class_name } */
char* db_get_all_subjects(db_connection_t *db)
{
    if (!db || !db->subjects_collection) return strdup("[]");

    DB_LOCK();
    bson_t *query = bson_new();
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(db->subjects_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char *)malloc(cap);
    if (!result) {
        mongoc_cursor_destroy(cursor); bson_destroy(query);
        DB_UNLOCK();
        return strdup("[]");
    }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t ni, ci;
        if (bson_iter_init_find(&ni, doc, "name")       && BSON_ITER_HOLDS_UTF8(&ni) &&
            bson_iter_init_find(&ci, doc, "class_name") && BSON_ITER_HOLDS_UTF8(&ci)) {
            const char *n = bson_iter_utf8(&ni, NULL);
            const char *c = bson_iter_utf8(&ci, NULL);
            char obj[512];
            snprintf(obj, sizeof(obj), "{\"name\":\"%s\",\"class_name\":\"%s\"}", n, c);
            if (!first) buf_append(&result, &len, &cap, ",");
            buf_append(&result, &len, &cap, obj);
            first = 0;
        }
    }
    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    DB_UNLOCK();
    return result;
}

/* Return subjects filtered by class as JSON array */
char* db_get_subjects_by_class(db_connection_t *db, const char *class_name)
{
    if (!db || !db->subjects_collection || !class_name) return strdup("[]");

    DB_LOCK();
    bson_t *query = BCON_NEW("class_name", class_name);
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(db->subjects_collection, query, NULL, NULL);

    size_t cap = 4096, len = 0;
    char *result = (char *)malloc(cap);
    if (!result) {
        mongoc_cursor_destroy(cursor); bson_destroy(query);
        DB_UNLOCK();
        return strdup("[]");
    }
    result[0] = '\0';
    buf_append(&result, &len, &cap, "[");

    const bson_t *doc;
    int first = 1;
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t ni, ci;
        if (bson_iter_init_find(&ni, doc, "name")       && BSON_ITER_HOLDS_UTF8(&ni) &&
            bson_iter_init_find(&ci, doc, "class_name") && BSON_ITER_HOLDS_UTF8(&ci)) {
            const char *n = bson_iter_utf8(&ni, NULL);
            const char *c = bson_iter_utf8(&ci, NULL);
            char obj[512];
            snprintf(obj, sizeof(obj), "{\"name\":\"%s\",\"class_name\":\"%s\"}", n, c);
            if (!first) buf_append(&result, &len, &cap, ",");
            buf_append(&result, &len, &cap, obj);
            first = 0;
        }
    }
    buf_append(&result, &len, &cap, "]");

    mongoc_cursor_destroy(cursor);
    bson_destroy(query);
    DB_UNLOCK();
    return result;
}

/* Delete a subject by name + class_name */
int db_delete_subject(db_connection_t *db, const char *name, const char *class_name)
{
    if (!db || !db->subjects_collection || !name || !class_name) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", name, "class_name", class_name);
    bool ok = mongoc_collection_delete_one(db->subjects_collection, filter, NULL, NULL, &error);
    bson_destroy(filter);

    if (!ok) { fprintf(stderr, "db_delete_subject: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

/* Rename a subject within a class */
int db_rename_subject(db_connection_t *db, const char *old_name, const char *class_name, const char *new_name)
{
    if (!db || !db->subjects_collection || !old_name || !class_name || !new_name) return 0;

    DB_LOCK();
    bson_error_t error;
    bson_t *filter = BCON_NEW("name", old_name, "class_name", class_name);
    bson_t *update = BCON_NEW("$set", "{", "name", new_name, "}");
    bool ok = mongoc_collection_update_one(db->subjects_collection, filter, update, NULL, NULL, &error);
    bson_destroy(filter);
    bson_destroy(update);

    if (!ok) { fprintf(stderr, "db_rename_subject: %s\n", error.message); DB_UNLOCK(); return 0; }
    DB_UNLOCK();
    return 1;
}

