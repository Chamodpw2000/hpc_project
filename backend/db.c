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
#include <sys/time.h>
#include <pthread.h>
#include <omp.h>
#include <curl/curl.h>

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

/* ── libcurl write callback for HTTP responses ── */
struct curl_buf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    struct curl_buf *buf = (struct curl_buf *)userdata;
    if (buf->len + bytes + 1 > buf->cap) {
        size_t new_cap = (buf->cap + bytes + 1) * 2;
        char *tmp = (char *)realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap  = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

/* ── Batch-fetch multiple random users in one HTTP request ──
 * randomuser.me supports ?results=N (up to 5000).
 * Parses the JSON array and fills names[]/emails[] arrays.
 * Returns number of users actually parsed (may be < count on partial failure).
 */
static int fetch_random_users_batch(int count,
                                    char names[][256],
                                    char emails[][256])
{
    if (count <= 0) return 0;
    if (count > 5000) count = 5000; /* API limit */

    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    char url[128];
    snprintf(url, sizeof(url), "https://randomuser.me/api/?results=%d", count);

    size_t init_cap = (size_t)count * 2048;
    if (init_cap < 8192) init_cap = 8192;
    struct curl_buf buf = { .data = (char *)malloc(init_cap), .len = 0, .cap = init_cap };
    if (!buf.data) { curl_easy_cleanup(curl); return 0; }
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) { free(buf.data); return 0; }

    /* Parse the results array.  Each user block has "name":{"first":"...","last":"..."}
     * and "email":"...".  We walk through the JSON string looking for repeated patterns. */
    int parsed = 0;
    char *cursor = buf.data;

    while (parsed < count) {
        /* Find next "name" object */
        char *name_obj = strstr(cursor, "\"name\"");
        if (!name_obj) break;

        char first[128] = "", last[128] = "", raw_email[256] = "";
        char *p;

        /* Extract "first" */
        p = strstr(name_obj, "\"first\"");
        if (p) {
            p = strchr(p + 7, ':');
            if (p) { p = strchr(p, '"'); if (p) { p++; char *end = strchr(p, '"');
                if (end) { size_t n = (size_t)(end - p); if (n >= sizeof(first)) n = sizeof(first)-1; memcpy(first, p, n); first[n] = '\0'; }
            }}
        }

        /* Extract "last" */
        p = strstr(name_obj, "\"last\"");
        if (p) {
            p = strchr(p + 6, ':');
            if (p) { p = strchr(p, '"'); if (p) { p++; char *end = strchr(p, '"');
                if (end) { size_t n = (size_t)(end - p); if (n >= sizeof(last)) n = sizeof(last)-1; memcpy(last, p, n); last[n] = '\0'; cursor = end + 1; }
            }}
        }

        /* Extract "email" – find the next "email" after the name object */
        p = strstr(cursor, "\"email\"");
        if (p) {
            p = strchr(p + 7, ':');
            if (p) { p = strchr(p, '"'); if (p) { p++; char *end = strchr(p, '"');
                if (end) { size_t n = (size_t)(end - p); if (n >= sizeof(raw_email)) n = sizeof(raw_email)-1; memcpy(raw_email, p, n); raw_email[n] = '\0'; cursor = end + 1; }
            }}
        }

        if (first[0] == '\0' || raw_email[0] == '\0') {
            /* Skip malformed entry, advance cursor past this block */
            cursor = name_obj + 6;
            continue;
        }

        /* Build full name */
        snprintf(names[parsed], 256, "%s %s", first, last);

        /* Make email unique: add index + microsecond digits before '@' */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        int ts_digits = (int)((tv.tv_usec + parsed) % 10000);

        char *at = strchr(raw_email, '@');
        if (at) {
            *at = '\0';
            snprintf(emails[parsed], 256, "%s%04d@%s", raw_email, ts_digits, at + 1);
        } else {
            snprintf(emails[parsed], 256, "%s%04d@example.com", raw_email, ts_digits);
        }

        parsed++;
    }

    free(buf.data);
    return parsed;
}

/* Seed dummy student and score data for OpenMP testing */
int db_seed_dummy_data(db_connection_t *db, int num_students, int scores_per_student)
{
    if (!db || !db->students_collection || !db->scores_collection) {
        return 0;
    }

    (void)scores_per_student; /* we now derive score count from subjects per class */

    DB_LOCK();

    srand((unsigned int)time(NULL));

    /* ── 1. Fetch all classes from the classes collection ── */
    bson_t *q_cls = bson_new();
    mongoc_cursor_t *cur_cls = mongoc_collection_find_with_opts(
        db->classes_collection, q_cls, NULL, NULL);

    char **class_names = NULL;
    int    num_classes = 0;
    {
        const bson_t *doc;
        size_t cls_cap = 32;
        class_names = (char **)malloc(cls_cap * sizeof(char *));
        while (mongoc_cursor_next(cur_cls, &doc)) {
            bson_iter_t it;
            if (bson_iter_init_find(&it, doc, "name") && BSON_ITER_HOLDS_UTF8(&it)) {
                const char *cn = bson_iter_utf8(&it, NULL);
                if ((size_t)num_classes >= cls_cap) {
                    cls_cap *= 2;
                    class_names = (char **)realloc(class_names, cls_cap * sizeof(char *));
                }
                class_names[num_classes++] = strdup(cn);
            }
        }
    }
    mongoc_cursor_destroy(cur_cls);
    bson_destroy(q_cls);

    if (num_classes == 0) {
        /* No classes in DB – can't seed meaningfully */
        if (class_names) free(class_names);
        DB_UNLOCK();
        fprintf(stderr, "db_seed_dummy_data: no classes found in database\n");
        return 0;
    }

    /* ── 2. For each class, fetch its subjects ── */
    char ***class_subjects   = (char ***)calloc((size_t)num_classes, sizeof(char **));
    int    *class_subj_count = (int *)calloc((size_t)num_classes, sizeof(int));

    for (int ci = 0; ci < num_classes; ci++) {
        bson_t *q_sub = BCON_NEW("class_name", class_names[ci]);
        mongoc_cursor_t *cur_sub = mongoc_collection_find_with_opts(
            db->subjects_collection, q_sub, NULL, NULL);

        size_t scap = 16;
        class_subjects[ci]   = (char **)malloc(scap * sizeof(char *));
        class_subj_count[ci] = 0;

        const bson_t *doc;
        while (mongoc_cursor_next(cur_sub, &doc)) {
            bson_iter_t it;
            if (bson_iter_init_find(&it, doc, "name") && BSON_ITER_HOLDS_UTF8(&it)) {
                const char *sn = bson_iter_utf8(&it, NULL);
                if ((size_t)class_subj_count[ci] >= scap) {
                    scap *= 2;
                    class_subjects[ci] = (char **)realloc(class_subjects[ci], scap * sizeof(char *));
                }
                class_subjects[ci][class_subj_count[ci]++] = strdup(sn);
            }
        }
        mongoc_cursor_destroy(cur_sub);
        bson_destroy(q_sub);
    }

    /* ── 3. Find the last student_id to continue numbering ── */
    bson_error_t error;
    int last_id_num = 0;
    {
        bson_t *opts = BCON_NEW("sort", "{", "student_id", BCON_INT32(-1), "}",
                                "limit", BCON_INT64(1));
        bson_t *q = bson_new();
        mongoc_cursor_t *cur = mongoc_collection_find_with_opts(
            db->students_collection, q, opts, NULL);
        const bson_t *doc;
        if (mongoc_cursor_next(cur, &doc)) {
            bson_iter_t it;
            if (bson_iter_init_find(&it, doc, "student_id") && BSON_ITER_HOLDS_UTF8(&it)) {
                const char *sid = bson_iter_utf8(&it, NULL);
                if (sid[0] == 'S') {
                    last_id_num = atoi(sid + 1);
                }
            }
        }
        mongoc_cursor_destroy(cur);
        bson_destroy(q);
        bson_destroy(opts);
    }
    printf("  Last existing student_id: S%05d (starting from S%05d)\n",
           last_id_num, last_id_num + 1);

    /* Release DB lock during the network-heavy fetch phase */
    DB_UNLOCK();

    /* ── 4. Parallel fetch of random users via OpenMP ──
     * Split num_students across OMP threads; each thread batch-fetches
     * its chunk from randomuser.me using ?results=N (up to 5000/call).
     * libcurl is thread-safe when each thread uses its own CURL handle. */

    /* Pre-allocate arrays for all student names and emails */
    typedef char name_buf_t[256];
    typedef char email_buf_t[256];
    name_buf_t  *all_names  = (name_buf_t *)calloc((size_t)num_students, sizeof(name_buf_t));
    email_buf_t *all_emails = (email_buf_t *)calloc((size_t)num_students, sizeof(email_buf_t));

    int num_threads = omp_get_max_threads();
    printf("[OpenMP] Fetching %d random users with %d threads...\n", num_students, num_threads);
    double t_fetch_start = omp_get_wtime();

    #pragma omp parallel
    {
        int tid       = omp_get_thread_num();
        int nthreads  = omp_get_num_threads();
        int chunk     = num_students / nthreads;
        int start_idx = tid * chunk;
        int end_idx   = (tid == nthreads - 1) ? num_students : start_idx + chunk;
        int my_count  = end_idx - start_idx;

        if (my_count > 0) {
            /* Temporary buffers for this thread's batch */
            name_buf_t  *tmp_names  = (name_buf_t *)calloc((size_t)my_count, sizeof(name_buf_t));
            email_buf_t *tmp_emails = (email_buf_t *)calloc((size_t)my_count, sizeof(email_buf_t));

            int fetched = fetch_random_users_batch(my_count, tmp_names, tmp_emails);

            printf("  Thread %d: fetched %d/%d users (range [%d..%d))\n",
                   tid, fetched, my_count, start_idx, end_idx);

            /* Copy fetched results into the global arrays */
            for (int i = 0; i < fetched; i++) {
                memcpy(all_names[start_idx + i],  tmp_names[i],  256);
                memcpy(all_emails[start_idx + i], tmp_emails[i], 256);
            }

            /* Fallback for any users that weren't fetched */
            for (int i = fetched; i < my_count; i++) {
                snprintf(all_names[start_idx + i],  256, "Student_%d", start_idx + i + 1);
                snprintf(all_emails[start_idx + i], 256, "student%d@university.edu", start_idx + i + 1);
            }

            free(tmp_names);
            free(tmp_emails);
        }
    }

    double t_fetch_end = omp_get_wtime();
    printf("[OpenMP] Fetch completed in %.2f seconds\n", t_fetch_end - t_fetch_start);

    /* ── 5. Serial DB inserts (mongoc is NOT thread-safe) ── */
    DB_LOCK();
    int total_scores = 0;

    for (int i = 0; i < num_students; i++) {
        /* Assign class sequentially (round-robin) */
        int ci = i % num_classes;
        const char *class_name = class_names[ci];

        /* Student ID */
        char student_id[32];
        snprintf(student_id, sizeof(student_id), "S%05d", last_id_num + i + 1);

        /* Insert student */
        bson_t *student_doc = bson_new();
        BSON_APPEND_UTF8(student_doc, "student_id", student_id);
        BSON_APPEND_UTF8(student_doc, "name", all_names[i]);
        BSON_APPEND_UTF8(student_doc, "email", all_emails[i]);
        BSON_APPEND_UTF8(student_doc, "class_name", class_name);
        BSON_APPEND_DATE_TIME(student_doc, "created_at", bson_get_monotonic_time());

        if (!mongoc_collection_insert_one(db->students_collection, student_doc, NULL, NULL, &error)) {
            fprintf(stderr, "Failed to insert student %s: %s\n", student_id, error.message);
        }
        bson_destroy(student_doc);

        printf("  Seeded student %d/%d: %s (%s) → %s\n",
               i + 1, num_students, all_names[i], student_id, class_name);

        /* Insert scores for each subject in this student's class */
        int n_subj = class_subj_count[ci];
        for (int j = 0; j < n_subj; j++) {
            const char *subject = class_subjects[ci][j];
            /* Random score between 0 and 100 */
            double score = ((double)rand() / RAND_MAX) * 100.0;

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

    /* ── 6. Clean up allocated memory ── */
    free(all_names);
    free(all_emails);

    for (int ci = 0; ci < num_classes; ci++) {
        for (int j = 0; j < class_subj_count[ci]; j++)
            free(class_subjects[ci][j]);
        free(class_subjects[ci]);
        free(class_names[ci]);
    }
    free(class_subjects);
    free(class_subj_count);
    free(class_names);

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

