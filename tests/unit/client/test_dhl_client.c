#define _GNU_SOURCE
#include "config.h"
#include "dhl_client.h"
#include "facade.h"
#include "logger.h"
#include "test_server.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE* saved_stdin = NULL;

void setUp(void)
{
    saved_stdin = stdin;
}

void tearDown(void)
{
    stdin = saved_stdin;
}

void test_start_client_failed(void)
{
    char* client = "0";
    int result = start_client(client);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_start_cli_failed(void)
{
    const char* input = "localhost\n9998\nuser_test\npass_test\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    int result = start_cli();
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_start_cli_success(void)
{
    const char* input = "localhost\n9999\nadmin\nadmin\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    int result = start_cli();
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void)
{
    test_server_t server_tcp;
    start_test_server(&server_tcp, (server_config_t){SERVER_MODE_TCP, SERVER_FAMILY_IPV4, 9999});
    UNITY_BEGIN();
    RUN_TEST(test_start_client_failed);
    RUN_TEST(test_start_cli_failed);
    RUN_TEST(test_start_cli_success);
    return UNITY_END();
}
