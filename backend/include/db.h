/*
 * MongoDB Database Helper for Score Analyzer Backend
 * Copyright (c) 2026
 * MIT License
 */

#ifndef DB_H
#define DB_H

#include <mongoc/mongoc.h>
#include <bson/bson.h>

/* Database connection structure */
typedef struct {
    mongoc_client_t *client;
    mongoc_database_t *database;
    mongoc_collection_t *students_collection;
    mongoc_collection_t *scores_collection;
    char *connection_string;
    int connected;
} db_connection_t;

/* Initialize database connection */
db_connection_t* db_init(const char *connection_string, const char *db_name);

/* Close database connection */
void db_cleanup(db_connection_t *db);

/* Test database connection */
int db_test_connection(db_connection_t *db);

/* Student operations */
int db_create_student(db_connection_t *db, const char *name, const char *email, const char *student_id);
char* db_get_all_students(db_connection_t *db);
char* db_get_student_by_id(db_connection_t *db, const char *student_id);
int db_update_student(db_connection_t *db, const char *student_id, const char *name, const char *email);
int db_delete_student(db_connection_t *db, const char *student_id);

/* Score operations */
int db_add_score(db_connection_t *db, const char *student_id, const char *subject, double score);
char* db_get_student_scores(db_connection_t *db, const char *student_id);
char* db_get_all_scores(db_connection_t *db);

/* Seed dummy data for OpenMP testing */
int db_seed_dummy_data(db_connection_t *db, int num_students, int scores_per_student);

/* Fetch all scores as a raw double array for computation */
double* db_get_scores_array(db_connection_t *db, int *out_count);

/* Get total count of scores */
int db_get_scores_count(db_connection_t *db);

#endif /* DB_H */
