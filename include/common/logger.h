#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define FILE_PATH 256

    // ==================== LOG LEVELS ====================

    typedef enum
    {
        LOG_DEBUG = 0,
        LOG_INFO = 1,
        LOG_WARNING = 2,
        LOG_ERROR = 3
    } log_level_t;

    // ==================== CONFIGURATION ====================

    typedef struct
    {
        char log_file_path[FILE_PATH]; // Path to the main log file
        size_t max_file_size;          // Maximum file size in bytes before rotation
        int max_backup_files;          // Maximum number of backup files to keep
        log_level_t min_level;         // Minimum log level to write (filters lower levels)
    } logger_config_t;

    // ==================== PUBLIC API ====================

    /**
     * @brief Initialize the logger with the specified configuration.
     *
     * This function sets up the logging system with rotation parameters.
     * Must be called before any logging operations.
     *
     * @param config Pointer to logger configuration structure
     * @return 0 on success, -1 on error
     *
     * @note Thread-safe: uses mutex for initialization
     *
     * Example:
     * @code
     * logger_config_t config = {
     *     .log_file_path = "/var/log/app.log",
     *     .max_file_size = 10 * 1024 * 1024,  // 10 MB
     *     .max_backup_files = 5,
     *     .min_level = LOG_INFO
     * };
     * if (log_init(&config) != 0) {
     *     fprintf(stderr, "Failed to initialize logger\n");
     *     return -1;
     * }
     * @endcode
     */
    int log_init(const logger_config_t* config);

    /**
     * @brief Write a log message with the specified level.
     *
     * Writes a formatted log message with timestamp and log level prefix.
     * Automatically rotates the log file if size threshold is exceeded.
     *
     * Format: [YYYY-MM-DD HH:MM:SS.mmmZ] [LEVEL] message
     *
     * @param level Log level (DEBUG, INFO, WARNING, ERROR)
     * @param format Printf-style format string
     * @param ... Variable arguments for format string
     *
     * @note Thread-safe: uses mutex for file operations
     * @note Messages below min_level are filtered out
     *
     * Example:
     * @code
     * log_write(LOG_INFO, "Server started on port %d", port);
     * log_write(LOG_ERROR, "Connection failed: %s", strerror(errno));
     * @endcode
     */
    void log_write(log_level_t level, const char* format, ...);

    /**
     * @brief Close the logger and release resources.
     *
     * Flushes any pending writes, closes the log file, and destroys the mutex.
     * Should be called before application exit.
     *
     * @note Thread-safe: uses mutex for cleanup
     *
     * Example:
     * @code
     * log_close();
     * @endcode
     */
    void log_close(void);

    /**
     * @brief Manually trigger log file rotation.
     *
     * Forces rotation of the current log file regardless of size.
     * Useful for time-based rotation or manual maintenance.
     *
     * @return 0 on success, -1 on error
     *
     * @note Thread-safe: uses mutex for file operations
     * @note Called automatically by log_write when size threshold is exceeded
     *
     * Example:
     * @code
     * // Rotate logs at midnight
     * log_rotate();
     * @endcode
     */
    int log_rotate(void);

    /**
     * @brief Get string representation of log level.
     *
     * @param level Log level enum value
     * @return Constant string ("DEBUG", "INFO", "WARNING", "ERROR", "UNKNOWN")
     *
     * @note Thread-safe: returns constant strings
     *
     * Example:
     * @code
     * const char* level_str = log_level_to_string(LOG_ERROR);
     * printf("Current level: %s\n", level_str);
     * @endcode
     */
    const char* log_level_to_string(log_level_t level);

// ==================== CONVENIENCE MACROS ====================

/**
 * Convenience macros for common log levels.
 * These provide a shorter syntax for logging.
 */
#define LOG_DEBUG_MSG(...) log_write(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO_MSG(...) log_write(LOG_INFO, __VA_ARGS__)
#define LOG_WARNING_MSG(...) log_write(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR_MSG(...) log_write(LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H
