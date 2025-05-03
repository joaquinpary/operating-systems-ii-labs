#include "logger.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEMP_LOG_FILE "test_log.txt"

void setUp(void)
{
    remove(TEMP_LOG_FILE);
    log_init(TEMP_LOG_FILE, "TEST");
}

void tearDown(void)
{
    remove(TEMP_LOG_FILE);
}

char* read_log_file(void)
{
    FILE* file = fopen(TEMP_LOG_FILE, "r");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char* content = malloc(size + 1);
    fread(content, 1, size, file);
    content[size] = '\0';
    fclose(file);
    return content;
}

void test_simple_info_log(void)
{
    log_message(LOG_LEVEL_INFO, "Hello from test");
    fflush(NULL);

    char* output = read_log_file();
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "Hello from test"));
    free(output);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_simple_info_log);
    return UNITY_END();
}
