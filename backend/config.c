/*
 * Configuration Loader Implementation
 * Copyright (c) 2026
 * MIT License
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Trim whitespace from string */
static void trim(char *str)
{
    if (!str) return;
    
    /* Trim leading */
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }
    
    /* Trim trailing */
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    
    /* Move trimmed string to start */
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/* Load configuration from file */
config_t* config_load(const char *filename)
{
    config_t *config = (config_t*)calloc(1, sizeof(config_t));
    if (!config) {
        fprintf(stderr, "Failed to allocate config\n");
        return NULL;
    }
    
    /* Set defaults */
    strcpy(config->mongodb_uri, "mongodb://localhost:27017/score_analyzer");
    strcpy(config->db_name, "score_analyzer");
    config->port = 8090;
    config->loaded = 0;
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "⚠️  Config file '%s' not found, using defaults\n", filename);
        fprintf(stderr, "   Create config.env from config.env.example\n");
        return config;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        /* Skip comments and empty lines */
        trim(line);
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        
        /* Parse KEY=VALUE */
        char *equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char *key = line;
            char *value = equals + 1;
            
            trim(key);
            trim(value);
            
            if (strcmp(key, "MONGODB_URI") == 0) {
                strncpy(config->mongodb_uri, value, sizeof(config->mongodb_uri) - 1);
            } else if (strcmp(key, "DB_NAME") == 0) {
                strncpy(config->db_name, value, sizeof(config->db_name) - 1);
            } else if (strcmp(key, "PORT") == 0) {
                config->port = atoi(value);
            }
        }
    }
    
    fclose(file);
    config->loaded = 1;
    
    printf("✓ Configuration loaded from %s\n", filename);
    return config;
}

/* Free configuration */
void config_free(config_t *config)
{
    if (config) {
        free(config);
    }
}

/* Get environment variable or default */
const char* config_get_env(const char *key, const char *default_value)
{
    const char *value = getenv(key);
    return value ? value : default_value;
}
