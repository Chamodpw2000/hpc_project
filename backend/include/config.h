/*
 * Configuration Loader for Score Analyzer Backend
 * Copyright (c) 2026
 * MIT License
 */

#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    char mongodb_uri[512];
    char db_name[128];
    int port;
    int loaded;
} config_t;

/* Load configuration from config.env file */
config_t* config_load(const char *filename);

/* Free configuration */
void config_free(config_t *config);

/* Get environment variable or default */
const char* config_get_env(const char *key, const char *default_value);

#endif /* CONFIG_H */
