#define _GNU_SOURCE
#include "behavior_cli.h"
#include "config.h"
#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include "test_server.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUCCESS "success"
#define FAILURE "failure"

#define AUTH_REQUEST 0
#define AUTH_RESPONSE "auth_response"
#define END_OF_MESSAGE "end_of_message"
#define TRANSACTION_HISTORY "server_transactions_history"
#define CLIENT_LIVES "all_clients_live"
#define CLI_TRANSACTION_HISTORY 1
#define CLI_ALL_CLIENT_LIVES 2
#define CLI_EXIT 3
#define ITEM_TYPE 6

static FILE* saved_stdin = NULL;

void setUp(void)
{
    saved_stdin = stdin;
}

void tearDown(void)
{
    stdin = saved_stdin;
}

void test_cli_authenticate_success_and_exit(void)
{
    const char* input = "admin\nadmin\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;

    init_params_client params = {
        .client_type = "admin",
        .connection_params = {.host = "localhost", .port = "8080", .protocol = "tcp", .ip_version = "ipv4"}};

    int sockfd = init_connection_tcp(params);
    TEST_ASSERT_TRUE(sockfd > 0);

    int result = logic_cli_sender_recv(params, sockfd);
    fclose(input_stream);
    close(sockfd);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_cli_authenticate_fail_three_times(void)
{
    const char* input = "wrong_user\nwrong_pass\nwrong_user\nwrong_pass\nwrong_user\nwrong_pass\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;

    init_params_client params = {
        .client_type = "admin",
        .connection_params = {.host = "localhost", .port = "8080", .protocol = "tcp", .ip_version = "ipv4"}};

    int sockfd = init_connection_tcp(params);
    TEST_ASSERT_TRUE(sockfd > 0);

    int result = logic_cli_sender_recv(params, sockfd);
    fclose(input_stream);
    close(sockfd);

    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_logic_cli_sender_recv_transactions(void)
{

    const char* input = "admin\nadmin\n1\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    init_params_client params = {
        .client_type = "admin",
        .connection_params = {.host = "localhost", .port = "8080", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    TEST_ASSERT_TRUE(sockfd > 0);
    int result = logic_cli_sender_recv(params, sockfd);
    fclose(input_stream);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_logic_cli_sender_recv_clients_lives(void)
{
    const char* input = "admin\nadmin\n2\n3\n";
    FILE* input_stream = fmemopen((void*)input, strlen(input), "r");
    TEST_ASSERT_NOT_NULL(input_stream);
    stdin = input_stream;
    init_params_client params = {
        .client_type = "admin",
        .connection_params = {.host = "localhost", .port = "8080", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    TEST_ASSERT_TRUE(sockfd > 0);
    int result = logic_cli_sender_recv(params, sockfd);
    fclose(input_stream);
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void)
{
    test_server_t test_server;
    start_test_server(&test_server,
                      (server_config_t){.mode = SERVER_MODE_TCP, .family = SERVER_FAMILY_IPV4, .port = 8080});
    UNITY_BEGIN();
    RUN_TEST(test_cli_authenticate_success_and_exit);
    RUN_TEST(test_cli_authenticate_fail_three_times);
    RUN_TEST(test_logic_cli_sender_recv_transactions);
    RUN_TEST(test_logic_cli_sender_recv_clients_lives);
    int result = UNITY_END();
    stop_test_server(&test_server);
    return result;
}
