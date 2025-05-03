#define _GNU_SOURCE
#include "behavior.h"
#include "behavior_cli.h"
#include "config.h"
#include "connection.h"
#include "facade.h"
#include "logger.h"
#include "shared_state.h"
#include "test_server.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE* saved_stdin = NULL;

void setUp(void)
{
    saved_stdin = stdin;
}

void tearDown(void)
{
    stdin = saved_stdin;
}

void test_connection_tcp()
{
    init_params_client params = {
        .client_id = "test_client",
        .client_type = "WAREHOUSE",
        .username = "user_test",
        .password = "pass_test",
        .connection_params =
            {
                .protocol = "tcp",
                .host = "localhost",
                .port = "8081",
            },
    };
    int result = connection(params);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_connection_udp()
{
    init_params_client params = {
        .client_id = "test_client",
        .client_type = "HUB",
        .username = "user_test",
        .password = "pass_test",
        .connection_params =
            {
                .protocol = "udp",
                .host = "localhost",
                .port = "8080",
            },
    };
    int result = connection(params);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_connection_cli_port_failed()
{
    init_params_client params = {
        .client_id = "admin",
        .client_type = "admin",
        .connection_params =
            {
                .ip_version = "ipv4",
                .protocol = "tcp",
                .host = "localhost",
                .port = "9999",
            },
    };
    const char* input = "admin\nadmin\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    int result = connection_cli(params);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_connection_cli_failed()
{
    init_params_client params = {
        .client_id = "admin",
        .client_type = "admin",
        .connection_params =
            {
                .ip_version = "ipv4",
                .protocol = "tcp",
                .host = "localhost",
                .port = "8081",
            },
    };
    const char* input = "wrong_user\nwrong_pass\nwrong_user\nwrong_pass\nwrong_user\nwrong_pass\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    int result = connection_cli(params);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_connection_cli_success()
{
    init_params_client params = {
        .client_id = "admin",
        .client_type = "admin",
        .connection_params =
            {
                .ip_version = "ipv4",
                .protocol = "tcp",
                .host = "localhost",
                .port = "8081",
            },
    };
    const char* input = "admin\nadmin\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    int result = connection_cli(params);
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void)
{
    test_server_t server_udp;
    test_server_t server_tcp;
    start_test_server(&server_udp, (server_config_t){.mode = SERVER_MODE_UDP, .port = 8080});
    start_test_server(&server_tcp, (server_config_t){.mode = SERVER_MODE_TCP, .port = 8081});
    UNITY_BEGIN();
    RUN_TEST(test_connection_tcp);
    RUN_TEST(test_connection_udp);
    RUN_TEST(test_connection_cli_port_failed);
    RUN_TEST(test_connection_cli_failed);
    RUN_TEST(test_connection_cli_success);
    int result = UNITY_END();
    stop_test_server(&server_udp);
    stop_test_server(&server_tcp);
    return result;
}
