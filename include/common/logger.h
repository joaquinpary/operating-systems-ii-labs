#ifndef LOGGER_H
#define LOGGER_H

#include <math.h>

typedef enum
{
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_DEBUG
} log_level;

static void rotate_logs();
void set_log_file(const char* filename);
void set_log_level(log_level level);
void set_log_component(const char* component);
void log_message(log_level level, const char* format, ...);
void log_init(const char* filename, const char* comp);

#define log_info(fmt, ...) log_message(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...) log_message(LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) log_message(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) log_message(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#endif
