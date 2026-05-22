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
    mongoc_client_pool_t *pool;       /* thread-safe client pool            */
    mongoc_client_t *client;          /* single client (used for init only) */
    mongoc_database_t *database;
    mongoc_collection_t *students_collection;
    mongoc_collection_t *scores_collection;
    mongoc_collection_t *classes_collection;
    mongoc_collection_t *subjects_collection;
    char *connection_string;
    char  db_name[256];   /* stored copy – safe after connection drops */
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
int db_create_student_with_class(db_connection_t *db, const char *name, const char *email, const char *student_id, const char *class_name);
char* db_get_all_students(db_connection_t *db);
char* db_get_students_by_class(db_connection_t *db, const char *class_name);
char* db_get_students_by_class_paginated(db_connection_t *db, const char *class_name, int page, int limit, const char *search);
int   db_count_students_by_class(db_connection_t *db, const char *class_name, const char *search);
char* db_get_student_by_id(db_connection_t *db, const char *student_id);
int db_update_student(db_connection_t *db, const char *student_id, const char *name, const char *email);
int db_delete_student(db_connection_t *db, const char *student_id);

/* Score operations */
int db_add_score(db_connection_t *db, const char *student_id, const char *subject, double score);
int db_update_score(db_connection_t *db, const char *student_id, const char *subject, double score);
int db_delete_score(db_connection_t *db, const char *student_id, const char *subject);
char* db_get_student_scores(db_connection_t *db, const char *student_id);
char* db_get_all_scores(db_connection_t *db);

/* Remove duplicate students (by student_id), keeping the first occurrence.
 * Also removes all scores for student_ids that had duplicates.
 * Returns number of duplicate student documents removed, or -1 on error. */
int db_remove_duplicate_students(db_connection_t *db, int *out_scores_removed);

/* Seed dummy data for OpenMP testing.
 * Pass class_filter != NULL to restrict all students to that one class;
 * NULL means round-robin across all existing classes.
 * Returns total scores inserted, or -1 if class_filter names a non-existent class. */
int db_seed_dummy_data(db_connection_t *db, int num_students, int scores_per_student,
                       const char *class_filter);

/* Fetch scores as a raw double array, optionally filtered by class and subject */
double* db_get_scores_array(db_connection_t *db, const char *class_filter, const char *subject_filter, int *out_count);

/* Get total count of scores */
int db_get_scores_count(db_connection_t *db);

/* Class operations */
int   db_create_class(db_connection_t *db, const char *name);
char* db_get_all_classes(db_connection_t *db);
int   db_rename_class(db_connection_t *db, const char *old_name, const char *new_name);
int   db_delete_class(db_connection_t *db, const char *name);

/* Subject operations */
int   db_create_subject(db_connection_t *db, const char *name, const char *class_name);
char* db_get_all_subjects(db_connection_t *db);
char* db_get_subjects_by_class(db_connection_t *db, const char *class_name);
int   db_get_class_subject_names(db_connection_t *db, const char *class_name, char ***out_names);
int   db_rename_subject(db_connection_t *db, const char *old_name, const char *class_name, const char *new_name);
int   db_delete_subject(db_connection_t *db, const char *name, const char *class_name);

/* Paired score for Pearson correlation computation */
typedef struct { double x; double y; } score_pair_t;

/*
 * Fetch paired (x, y) scores for two subjects filtered by their respective classes.
 * Joins on student_id in-memory using a sorted merge-join.
 * Caller must free(*out_pairs).
 * Returns 1 on success (at least 1 pair found), 0 on failure.
 */
int db_get_paired_scores(db_connection_t *db,
                         const char *subject1, const char *class1,
                         const char *subject2, const char *class2,
                         score_pair_t **out_pairs, int *out_count,
                         int *out_total_students, int *out_excluded,
                         double *out_fetch_ms);

#endif /* DB_H */
