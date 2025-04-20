#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE* log_file = NULL;
static log_level current_level = LOG_LEVEL_INFO;
static char component[32] = "UNKNOWN";

void set_log_file(const char* filename)
{
    log_file = fopen(filename, "a");
    if (!log_file)
    {
        perror("Could not open log file");
        log_file = stderr;
    }
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

    if (!log_file)
        log_file = stderr;

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
