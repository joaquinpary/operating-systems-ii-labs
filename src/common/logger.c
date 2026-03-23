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

#define TIMESTAMP_BUFFER_SIZE 64
#define LOG_PATH_BUFFER_SIZE 512
#define DEFAULT_LOG_MAX_FILE_SIZE (10 * 1024 * 1024)
#define DEFAULT_LOG_MAX_BACKUPS 5

static logger_config_t g_config;
static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_initialized = 0;
static int g_next_index = 1;

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
 * @brief Scan existing backup files to find the next available index.
 * Called once at init so rotate_files() can be O(1).
 */
static void scan_existing_backups(void)
{
    char path[LOG_PATH_BUFFER_SIZE];
    g_next_index = 1;
    while (1)
    {
        snprintf(path, sizeof(path), "%s.%d", g_config.log_file_path, g_next_index);
        if (access(path, F_OK) != 0)
            break;
        g_next_index++;
    }
}

/**
 * @brief Rotate log files using O(1) append-forward scheme.
 * app.log -> app.log.N (single rename, no shifting).
 * .1 = oldest, .N = newest. Deletes excess old backups.
 */
static int rotate_files(void)
{
    char new_name[LOG_PATH_BUFFER_SIZE];

    snprintf(new_name, sizeof(new_name), "%s.%d", g_config.log_file_path, g_next_index);
    if (rename(g_config.log_file_path, new_name) != 0)
    {
        fprintf(stderr, "[LOGGER] Failed to rename current log file: %s\n", strerror(errno));
        return -1;
    }
    g_next_index++;

    if (g_config.max_backup_files > 0)
    {
        int oldest = g_next_index - g_config.max_backup_files - 1;
        if (oldest >= 1)
        {
            char old_name[LOG_PATH_BUFFER_SIZE];
            snprintf(old_name, sizeof(old_name), "%s.%d", g_config.log_file_path, oldest);
            remove(old_name);
        }
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

    long current_size = get_file_size(g_config.log_file_path);

    if (current_size >= (long)g_config.max_file_size)
    {
        fclose(g_log_file);
        g_log_file = NULL;

        if (rotate_files() != 0)
        {
            fprintf(stderr, "[LOGGER] File rotation failed\n");
        }

        g_log_file = fopen(g_config.log_file_path, "a");
        if (g_log_file == NULL)
        {
            fprintf(stderr, "[LOGGER] Failed to reopen log file: %s\n", strerror(errno));
            return -1;
        }

        setvbuf(g_log_file, NULL, _IOLBF, 0);
    }

    return 0;
}

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

    if (g_initialized)
    {
        fprintf(stderr, "[LOGGER] Already initialized\n");
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    memcpy(&g_config, config, sizeof(logger_config_t));

    if (g_config.max_file_size == 0)
    {
        g_config.max_file_size = DEFAULT_LOG_MAX_FILE_SIZE;
    }

    if (g_config.max_backup_files < 0)
    {
        g_config.max_backup_files = DEFAULT_LOG_MAX_BACKUPS;
    }

    scan_existing_backups();

    g_log_file = fopen(g_config.log_file_path, "a");
    if (g_log_file == NULL)
    {
        fprintf(stderr, "[LOGGER] Failed to open log file '%s': %s\n", g_config.log_file_path, strerror(errno));
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }

    setvbuf(g_log_file, NULL, _IOLBF, 0);

    g_initialized = 1;

    pthread_mutex_unlock(&g_log_mutex);

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

    check_and_rotate();

    char timestamp[TIMESTAMP_BUFFER_SIZE];
    get_timestamp(timestamp, sizeof(timestamp));

    const char* level_str = log_level_to_string(level);

    fprintf(g_log_file, "[%s] [%s] ", timestamp, level_str);

    va_list args;
    va_start(args, format);
    vfprintf(g_log_file, format, args);
    va_end(args);

    fprintf(g_log_file, "\n");

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
        char timestamp[TIMESTAMP_BUFFER_SIZE];
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

    fclose(g_log_file);
    g_log_file = NULL;

    int result = rotate_files();

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
