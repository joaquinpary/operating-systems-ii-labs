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

// ==================== INITIALIZATION TESTS ====================

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

// ==================== LOGGING TESTS ====================

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

// ==================== ROTATION TESTS ====================

void test_log_rotation_on_size(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = 512, // Small size to trigger rotation
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};

    log_init(&config);
    
    // Write many messages to exceed max_file_size
    for (int i = 0; i < 50; i++)
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
    
    // Write enough to trigger multiple rotations
    for (int i = 0; i < 100; i++)
    {
        log_write(LOG_INFO, "Rotation test message number %d with padding text", i);
    }
    
    log_close();
    
    // Verify main file exists
    TEST_ASSERT_EQUAL(0, access(TEST_LOG_FILE, F_OK));
    
    // Verify backup files .1 and .2 exist
    TEST_ASSERT_EQUAL(0, access("/tmp/test_app.log.1", F_OK));
    TEST_ASSERT_EQUAL(0, access("/tmp/test_app.log.2", F_OK));
    
    // Verify .3 does NOT exist (max_backup_files = 2)
    TEST_ASSERT_NOT_EQUAL(0, access("/tmp/test_app.log.3", F_OK));
}

// ==================== CONCURRENCY TESTS ====================

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

// ==================== MAIN ====================

int main(void)
{
    UNITY_BEGIN();

    // Initialization tests
    RUN_TEST(test_log_init_success);
    RUN_TEST(test_log_init_null_config);
    RUN_TEST(test_log_init_double_init);

    // Logging tests
    RUN_TEST(test_log_write_basic);
    RUN_TEST(test_log_level_filtering);
    RUN_TEST(test_log_level_to_string);

    // Rotation tests
    RUN_TEST(test_log_rotation_on_size);
    RUN_TEST(test_log_manual_rotation);
    RUN_TEST(test_log_max_backup_files);

    // Concurrency tests
    RUN_TEST(test_concurrent_writes);

    return UNITY_END();
}
