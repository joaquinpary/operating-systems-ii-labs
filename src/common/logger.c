#define _POSIX_C_SOURCE 200809L
#include "logger.h"
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// ==================== PRIVATE STATE ====================

static logger_config_t g_config;
static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_initialized = 0;

// ==================== PRIVATE FUNCTIONS ====================

/**
 * @brief Get current timestamp in ISO 8601 format with milliseconds
 * Format: YYYY-MM-DD HH:MM:SS.mmmZ
 */
static void get_timestamp(char* buffer, size_t size)
{
    struct timeval tv;
    struct tm* tm_info;

    gettimeofday(&tv, NULL);
    tm_info = gmtime(&tv.tv_sec);

    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);

    // Append milliseconds
    snprintf(buffer + strlen(buffer), size - strlen(buffer), ".%03ldZ", tv.tv_usec / 1000);
}

/**
 * @brief Get the current size of the log file
 */
static long get_file_size(const char* filename)
{
    struct stat st;
    if (stat(filename, &st) == 0)
    {
        return st.st_size;
    }
    return 0;
}

/**
 * @brief Rotate log files: app.log -> app.log.1 -> app.log.2 -> ... -> delete oldest
 */
static int rotate_files(void)
{
    char old_name[512];
    char new_name[512];

    // Delete the oldest file if it exists (e.g., app.log.5)
    if (g_config.max_backup_files > 0)
    {
        snprintf(old_name, sizeof(old_name), "%s.%d", g_config.log_file_path, g_config.max_backup_files);
        if (access(old_name, F_OK) == 0)
        {
            if (remove(old_name) != 0)
            {
                fprintf(stderr, "[LOGGER] Failed to remove oldest log file: %s\n", old_name);
            }
        }
    }

    // Rename existing backup files: app.log.3 -> app.log.4, app.log.2 -> app.log.3, etc.
    for (int i = g_config.max_backup_files - 1; i >= 1; i--)
    {
        snprintf(old_name, sizeof(old_name), "%s.%d", g_config.log_file_path, i);
        snprintf(new_name, sizeof(new_name), "%s.%d", g_config.log_file_path, i + 1);

        if (access(old_name, F_OK) == 0)
        {
            if (rename(old_name, new_name) != 0)
            {
                fprintf(stderr, "[LOGGER] Failed to rename %s -> %s: %s\n", old_name, new_name, strerror(errno));
                return -1;
            }
        }
    }

    // Rename current log file to .1
    snprintf(new_name, sizeof(new_name), "%s.1", g_config.log_file_path);
    if (rename(g_config.log_file_path, new_name) != 0)
    {
        fprintf(stderr, "[LOGGER] Failed to rename current log file: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * @brief Check if rotation is needed and perform it
 */
static int check_and_rotate(void)
{
    if (g_log_file == NULL)
    {
        return -1;
    }

    // Get current file size
    long current_size = get_file_size(g_config.log_file_path);

    if (current_size >= (long)g_config.max_file_size)
    {
        // Close current file
        fclose(g_log_file);
        g_log_file = NULL;

        // Rotate files
        if (rotate_files() != 0)
        {
            fprintf(stderr, "[LOGGER] File rotation failed\n");
            // Try to reopen the file anyway
        }

        // Open new log file
        g_log_file = fopen(g_config.log_file_path, "a");
        if (g_log_file == NULL)
        {
            fprintf(stderr, "[LOGGER] Failed to reopen log file: %s\n", strerror(errno));
            return -1;
        }

        // Set line buffering for immediate writes
        setvbuf(g_log_file, NULL, _IOLBF, 0);
    }

    return 0;
}

// ==================== PUBLIC API IMPLEMENTATION ====================

const char* log_level_to_string(log_level_t level)
{
    switch (level)
    {
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_INFO:
        return "INFO";
    case LOG_WARNING:
        return "WARNING";
    case LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

int log_init(const logger_config_t* config)
{
    if (config == NULL)
    {
        fprintf(stderr, "[LOGGER] NULL configuration provided\n");
        return -1;
    }

    pthread_mutex_lock(&g_log_mutex);

    // Check if already initialized
    if (g_initialized)
    {
        fprintf(stderr, "[LOGGER] Already initialized\n");
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    // Copy configuration
    memcpy(&g_config, config, sizeof(logger_config_t));

    // Validate configuration
    if (g_config.max_file_size == 0)
    {
        g_config.max_file_size = 10 * 1024 * 1024; // Default: 10 MB
    }

    if (g_config.max_backup_files < 0)
    {
        g_config.max_backup_files = 5; // Default: 5 backup files
    }

    // Open log file in append mode
    g_log_file = fopen(g_config.log_file_path, "a");
    if (g_log_file == NULL)
    {
        fprintf(stderr, "[LOGGER] Failed to open log file '%s': %s\n", g_config.log_file_path, strerror(errno));
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    // Set line buffering for immediate writes
    setvbuf(g_log_file, NULL, _IOLBF, 0);

    g_initialized = 1;

    pthread_mutex_unlock(&g_log_mutex);

    // Log initialization
    log_write(LOG_INFO, "Logger initialized: max_size=%zu bytes, max_backups=%d", g_config.max_file_size,
              g_config.max_backup_files);

    return 0;
}

void log_write(log_level_t level, const char* format, ...)
{
    if (!g_initialized)
    {
        fprintf(stderr, "[LOGGER] Not initialized, call log_init() first\n");
        return;
    }

    // Filter messages below minimum level
    if (level < g_config.min_level)
    {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    if (g_log_file == NULL)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    // Check if rotation is needed
    check_and_rotate();

    // Get timestamp
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Get log level string
    const char* level_str = log_level_to_string(level);

    // Write log prefix: [timestamp] [LEVEL]
    fprintf(g_log_file, "[%s] [%s] ", timestamp, level_str);

    // Write user message
    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);

    // Write newline
    fprintf(g_log_file, "\n");

    // Flush to ensure write (line buffering should handle this, but explicit is safer)
    fflush(g_log_file);

    pthread_mutex_unlock(&g_log_mutex);
}

void log_close(void)
{
    if (!g_initialized)
    {
        return;
    }

    pthread_mutex_lock(&g_log_mutex);

    if (g_log_file != NULL)
    {
        // Write shutdown message directly without calling log_write (avoid deadlock)
        char timestamp[64];
        get_timestamp(timestamp, sizeof(timestamp));
        fprintf(g_log_file, "[%s] [INFO] Logger shutting down\n", timestamp);
        fflush(g_log_file);

        fclose(g_log_file);
        g_log_file = NULL;
    }

    g_initialized = 0;

    pthread_mutex_unlock(&g_log_mutex);
}

int log_rotate(void)
{
    if (!g_initialized)
    {
        fprintf(stderr, "[LOGGER] Not initialized\n");
        return -1;
    }

    pthread_mutex_lock(&g_log_mutex);

    if (g_log_file == NULL)
    {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    // Close current file
    fclose(g_log_file);
    g_log_file = NULL;

    // Rotate files
    int result = rotate_files();

    // Reopen log file
    g_log_file = fopen(g_config.log_file_path, "a");
    if (g_log_file == NULL)
    {
        fprintf(stderr, "[LOGGER] Failed to reopen log file after rotation: %s\n", strerror(errno));
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    setvbuf(g_log_file, NULL, _IOLBF, 0);

    pthread_mutex_unlock(&g_log_mutex);

    log_write(LOG_INFO, "Log file rotated manually");

    return result;
}
