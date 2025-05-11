#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TAG_SIZE 10
#define LOG_NAME_SIZE 50
#define MAX_LOG_SIZE (1024 * 1024) // 1 MB
#define MAX_LOG_FILES 5


static FILE* log_file = NULL;
static log_level current_level = LOG_LEVEL_INFO;
static char component[TAG_SIZE] = "UNKNOWN";
static char log_filename[LOG_NAME_SIZE] = "log.txt";

static void rotate_logs()
{
    char old_name[512], new_name[512];

    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }

    snprintf(old_name, sizeof(old_name), "%s.%d", log_filename, MAX_LOG_FILES);
    remove(old_name);

    for (int i = MAX_LOG_FILES - 1; i >= 1; --i)
    {
        snprintf(old_name, sizeof(old_name), "%s.%d", log_filename, i);
        snprintf(new_name, sizeof(new_name), "%s.%d", log_filename, i + 1);
        rename(old_name, new_name);
    }

    snprintf(new_name, sizeof(new_name), "%s.1", log_filename);
    rename(log_filename, new_name);
}

static void open_log_file()
{
    if (!log_file)
    {
        log_file = fopen(log_filename, "a");
        if (!log_file)
        {
            perror("Could not open log file");
            log_file = stderr;
        }
    }
}

void set_log_file(const char* filename)
{
    strncpy(log_filename, filename, sizeof(log_filename) - 1);
    log_filename[sizeof(log_filename) - 1] = '\0';

    open_log_file();
}

void set_log_level(log_level level)
{
    current_level = level;
}

void set_log_component(const char* comp)
{
    strncpy(component, comp, sizeof(component) - 1);
    component[sizeof(component) - 1] = '\0';
}

static const char* level_to_string(log_level level)
{
    switch (level)
    {
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARNING:
        return "WARNING";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

void log_message(log_level level, const char* format, ...)
{
    if (level > current_level)
        return;
    struct stat st;
    if (stat(log_filename, &st) == 0 && st.st_size >= MAX_LOG_SIZE)
    {
        if (log_file)
        {
            fclose(log_file);
            log_file = NULL;
        }

        rotate_logs();
    }

    if (!log_file)
    {
        log_file = fopen(log_filename, "a");
        if (!log_file)
        {
            perror("Could not open log file");
            log_file = stderr;
        }
    }

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);

    fprintf(log_file, "[%s] [%s] [%s] ", time_buf, component, level_to_string(level));

    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}

void log_init(const char* filename, const char* comp)
{
    set_log_file(filename);
    set_log_component(comp);
}
