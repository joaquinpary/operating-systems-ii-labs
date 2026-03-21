#include "logger.h"
#include "unity.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_LOG_FILE "/tmp/test_app.log"
#define TEST_LOG_SIZE 1024 // 1 KB for fast testing

void setUp(void)
{
    // Clean up any existing test log files
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -f %s %s.*", TEST_LOG_FILE, TEST_LOG_FILE);
    system(cmd);
}

void tearDown(void)
{
    log_close();
    
    // Clean up test files
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -f %s %s.*", TEST_LOG_FILE, TEST_LOG_FILE);
    system(cmd);
}

void test_log_init_success(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    TEST_ASSERT_EQUAL(0, log_init(&config));
    
    // Verify file was created
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);
}

void test_log_init_null_config(void)
{
    TEST_ASSERT_EQUAL(-1, log_init(NULL));
}

void test_log_init_double_init(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    TEST_ASSERT_EQUAL(0, log_init(&config));
    
    // Second init should fail
    TEST_ASSERT_EQUAL(-1, log_init(&config));
}

void test_log_write_basic(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);
    
    log_write(LOG_INFO, "Test message");
    log_write(LOG_ERROR, "Error message with number: %d", 42);
    
    log_close();
    
    // Verify messages were written
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    
    char line[512];
    int found_info = 0, found_error = 0;
    
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strstr(line, "[INFO]") && strstr(line, "Test message"))
        {
            found_info = 1;
        }
        if (strstr(line, "[ERROR]") && strstr(line, "Error message with number: 42"))
        {
            found_error = 1;
        }
    }
    
    fclose(f);
    
    TEST_ASSERT_EQUAL(1, found_info);
    TEST_ASSERT_EQUAL(1, found_error);
}

void test_log_level_filtering(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE,
                              .max_backup_files = 3,
                              .min_level = LOG_WARNING}; // Filter DEBUG and INFO

    log_init(&config);
    
    log_write(LOG_DEBUG, "Debug message - should be filtered");
    log_write(LOG_INFO, "Info message - should be filtered");
    log_write(LOG_WARNING, "Warning message - should appear");
    log_write(LOG_ERROR, "Error message - should appear");
    
    log_close();
    
    // Verify only WARNING and ERROR appear
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    
    char line[512];
    int found_debug = 0, found_info = 0, found_warning = 0, found_error = 0;
    
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (strstr(line, "Debug message")) found_debug = 1;
        if (strstr(line, "Info message")) found_info = 1;
        if (strstr(line, "Warning message")) found_warning = 1;
        if (strstr(line, "Error message")) found_error = 1;
    }
    
    fclose(f);
    
    TEST_ASSERT_EQUAL(0, found_debug);
    TEST_ASSERT_EQUAL(0, found_info);
    TEST_ASSERT_EQUAL(1, found_warning);
    TEST_ASSERT_EQUAL(1, found_error);
}

void test_log_level_to_string(void)
{
    TEST_ASSERT_EQUAL_STRING("DEBUG", log_level_to_string(LOG_DEBUG));
    TEST_ASSERT_EQUAL_STRING("INFO", log_level_to_string(LOG_INFO));
    TEST_ASSERT_EQUAL_STRING("WARNING", log_level_to_string(LOG_WARNING));
    TEST_ASSERT_EQUAL_STRING("ERROR", log_level_to_string(LOG_ERROR));
}

void test_log_rotation_on_size(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = 512, // Small size to trigger rotation
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);

    // Write just enough messages to trigger one rotation (each ~80 bytes, limit 512)
    for (int i = 0; i < 10; i++)
    {
        log_write(LOG_INFO, "Message number %d with some padding to increase size", i);
    }

    log_close();
    
    // Verify rotation occurred - backup files should exist
    FILE* f1 = fopen(TEST_LOG_FILE, "r");
    FILE* f2 = fopen("/tmp/test_app.log.1", "r");
    
    TEST_ASSERT_NOT_NULL(f1); // Main file exists
    TEST_ASSERT_NOT_NULL(f2); // Backup file exists
    
    fclose(f1);
    fclose(f2);
}

void test_log_manual_rotation(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);
    
    log_write(LOG_INFO, "Before rotation");
    
    // Manual rotation
    TEST_ASSERT_EQUAL(0, log_rotate());
    
    log_write(LOG_INFO, "After rotation");
    
    log_close();
    
    // Verify backup file exists
    FILE* f_backup = fopen("/tmp/test_app.log.1", "r");
    TEST_ASSERT_NOT_NULL(f_backup);
    
    // Verify "Before rotation" is in backup
    char line[512];
    int found_before = 0;
    while (fgets(line, sizeof(line), f_backup) != NULL)
    {
        if (strstr(line, "Before rotation"))
        {
            found_before = 1;
            break;
        }
    }
    fclose(f_backup);
    TEST_ASSERT_EQUAL(1, found_before);
    
    // Verify "After rotation" is in main file
    FILE* f_main = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f_main);
    
    int found_after = 0;
    while (fgets(line, sizeof(line), f_main) != NULL)
    {
        if (strstr(line, "After rotation"))
        {
            found_after = 1;
            break;
        }
    }
    fclose(f_main);
    TEST_ASSERT_EQUAL(1, found_after);
}

void test_log_max_backup_files(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = 256, // Very small for fast rotation
                              .max_backup_files = 2, // Keep only 2 backups
                              .min_level = LOG_DEBUG};

    log_init(&config);

    // Write enough to trigger multiple rotations (each ~80 bytes, limit 256)
    for (int i = 0; i < 100; i++)
    {
        log_write(LOG_INFO, "Rotation test message number %d with padding text", i);
    }

    log_close();

    // Verify main file exists
    TEST_ASSERT_EQUAL(0, access(TEST_LOG_FILE, F_OK));

    // Logger uses append-forward rotation (.1=oldest, .N=newest).
    // After many rotations with max_backup_files=2, only the 2 most recent
    // backups survive; their exact indices depend on the total rotation count.
    // Count all existing backup files and assert the ceiling is respected.
    int backup_count = 0;
    char path[256];
    for (int i = 1; i <= 200; i++)
    {
        snprintf(path, sizeof(path), "%s.%d", TEST_LOG_FILE, i);
        if (access(path, F_OK) == 0)
        {
            backup_count++;
        }
    }
    TEST_ASSERT_EQUAL(2, backup_count);
}

typedef struct
{
    int thread_id;
    int message_count;
} thread_args_t;

void* concurrent_writer(void* arg)
{
    thread_args_t* args = (thread_args_t*)arg;
    
    for (int i = 0; i < args->message_count; i++)
    {
        log_write(LOG_INFO, "Thread %d - Message %d", args->thread_id, i);
    }
    
    return NULL;
}

void test_concurrent_writes(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = 10 * 1024, // 10 KB
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);
    
    // Create multiple threads writing concurrently
    const int num_threads = 5;
    const int messages_per_thread = 20;
    pthread_t threads[num_threads];
    thread_args_t args[num_threads];
    
    for (int i = 0; i < num_threads; i++)
    {
        args[i].thread_id = i;
        args[i].message_count = messages_per_thread;
        pthread_create(&threads[i], NULL, concurrent_writer, &args[i]);
    }
    
    // Wait for all threads
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }
    
    log_close();
    
    // Verify all messages were written (count lines)
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    
    char line[512];
    int line_count = 0;
    
    while (fgets(line, sizeof(line), f) != NULL)
    {
        // Skip initialization message
        if (strstr(line, "Logger initialized") == NULL && strstr(line, "Logger shutting down") == NULL)
        {
            line_count++;
        }
    }
    fclose(f);
    
    // Should have num_threads * messages_per_thread lines
    TEST_ASSERT_EQUAL(num_threads * messages_per_thread, line_count);
}

void test_log_write_before_init(void)
{
    log_close();
    
    log_write(LOG_INFO, "This should not be written");
    
    TEST_PASS();
}

void test_log_rotate_before_init(void)
{
    log_close();
    
    TEST_ASSERT_EQUAL(-1, log_rotate());
}

void test_log_close_when_not_init(void)
{
    log_close();
    
    log_close();
    
    TEST_PASS();
}

void test_log_level_unknown(void)
{
    // Test with an invalid log level value
    const char* result = log_level_to_string((log_level_t)999);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", result);
}

void test_log_all_levels(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_SIZE * 10,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);
    
    log_write(LOG_DEBUG, "Debug message");
    log_write(LOG_INFO, "Info message");
    log_write(LOG_WARNING, "Warning message");
    log_write(LOG_ERROR, "Error message");
    
    log_close();
    
    // Verify all levels were written
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    
    char content[4096] = {0};
    fread(content, 1, sizeof(content) - 1, f);
    fclose(f);
    
    TEST_ASSERT_NOT_NULL(strstr(content, "[DEBUG]"));
    TEST_ASSERT_NOT_NULL(strstr(content, "[INFO]"));
    TEST_ASSERT_NOT_NULL(strstr(content, "[WARNING]"));
    TEST_ASSERT_NOT_NULL(strstr(content, "[ERROR]"));
}

void test_log_init_default_values(void)
{
    // Test with 0 values to trigger defaults
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = 0,        // Should use default 10MB
                              .max_backup_files = -1,    // Should use default 5
                              .min_level = LOG_DEBUG};

    TEST_ASSERT_EQUAL(0, log_init(&config));
    
    // Write something to verify it works
    log_write(LOG_INFO, "Test with defaults");
    
    log_close();
    
    // Verify file was created
    FILE* f = fopen(TEST_LOG_FILE, "r");
    TEST_ASSERT_NOT_NULL(f);
    fclose(f);
}

// ==================== MAIN ====================

int main(void)
{
    UNITY_BEGIN();

    // Initialization tests
    RUN_TEST(test_log_init_success);
    RUN_TEST(test_log_init_null_config);
    RUN_TEST(test_log_init_double_init);
    RUN_TEST(test_log_init_default_values);

    // Logging tests
    RUN_TEST(test_log_write_basic);
    RUN_TEST(test_log_level_filtering);
    RUN_TEST(test_log_level_to_string);
    RUN_TEST(test_log_level_unknown);
    RUN_TEST(test_log_all_levels);

    // Error handling tests
    RUN_TEST(test_log_write_before_init);
    RUN_TEST(test_log_rotate_before_init);
    RUN_TEST(test_log_close_when_not_init);

    // Rotation tests
    RUN_TEST(test_log_rotation_on_size);
    RUN_TEST(test_log_manual_rotation);
    RUN_TEST(test_log_max_backup_files);

    // Concurrency tests
    RUN_TEST(test_concurrent_writes);

    return UNITY_END();
}
