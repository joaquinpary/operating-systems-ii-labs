#include "connection.h"
#include "json_manager.h"
#include "test_server.h"
#include "unity.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define TEST_IPV4 "127.0.0.1"
#define TEST_IPV6 "::1"
#define TEST_PORT_TCP "12345"
#define TEST_PORT_UDP "54321"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_setup_socket_udp_ipv4_valid(void)
{
    // start_test_server((server_config_t){.mode = SERVER_MODE_UDP, .family = SERVER_FAMILY_IPV4, .port =
    // atoi(TEST_PORT_UDP)});
    struct sockaddr_storage dest_addr;
    socklen_t addr_len;

    int sockfd = setup_socket_udp(IPV4, TEST_IPV4, TEST_PORT_UDP, &dest_addr, &addr_len);

    TEST_ASSERT_GREATER_THAN(0, sockfd);
    TEST_ASSERT_GREATER_THAN(0, addr_len);
    // sender_udp(sockfd, "quit", &dest_addr, addr_len);
    close(sockfd);
    // stop_test_server();
}

void test_setup_socket_udp_invalid_ip(void)
{
    struct sockaddr_storage dest_addr;
    socklen_t addr_len;
    int sockfd = setup_socket_udp(IPV4, "256.256.256.256", TEST_PORT_UDP, &dest_addr, &addr_len);
    TEST_ASSERT_EQUAL(-1, sockfd);
}

void test_sender_udp_invalid_socket(void)
{
    struct sockaddr_storage dest_addr;
    socklen_t addr_len = 0;
    char buffer[] = "test";
    int result = sender_udp(-1, buffer, &dest_addr, addr_len);
    TEST_ASSERT_EQUAL(1, result);
}

void test_receiver_udp_invalid_socket(void)
{
    struct sockaddr_storage dest_addr;
    socklen_t addr_len = 0;
    char buffer[SOCKET_SIZE];
    int result = receiver_udp(-1, buffer, &dest_addr, addr_len);
    TEST_ASSERT_EQUAL(1, result);
}

void test_setup_socket_tcp_invalid_ip(void)
{
    int sockfd = setup_socket_tpc(IPV4, "256.256.256.256", TEST_PORT_TCP);
    TEST_ASSERT_EQUAL(-1, sockfd);
}

void test_sender_tcp_invalid_socket(void)
{
    char buffer[] = "test";
    int result = sender_tpc(-1, buffer);
    TEST_ASSERT_EQUAL(1, result);
}

void test_receiver_tcp_invalid_socket(void)
{
    char buffer[SOCKET_SIZE];
    int result = receiver_tcp(-1, buffer);
    TEST_ASSERT_EQUAL(1, result);
}

void test_init_connection_udp_valid(void)
{
    init_params_client params = {.connection_params = {.ip_version = IPV4, .host = TEST_IPV4, .port = TEST_PORT_UDP}};

    connection_context context = init_connection_udp(params);

    TEST_ASSERT_GREATER_THAN(0, context.sockfd);
    TEST_ASSERT_GREATER_THAN(0, context.addr_len);
    // sender_udp(context.sockfd, "quit", &context.dest_addr, context.addr_len);
    close(context.sockfd);
    // stop_test_server();
}

void test_init_connection_udp_invalid_ip(void)
{
    init_params_client params = {
        .connection_params = {.ip_version = IPV4, .host = "256.256.256.256", .port = TEST_PORT_UDP}};

    connection_context context = init_connection_udp(params);
    TEST_ASSERT_EQUAL(-1, context.sockfd);
}

void test_init_connection_tcp_valid(void)
{
    // start_test_server((server_config_t){.mode = SERVER_MODE_TCP, .family = SERVER_FAMILY_IPV4, .port =
    // atoi(TEST_PORT_TCP)});
    init_params_client params = {.connection_params = {.ip_version = IPV4, .host = TEST_IPV4, .port = TEST_PORT_TCP}};

    int sockfd = init_connection_tcp(params);

    TEST_ASSERT_GREATER_THAN(0, sockfd);
    // sender_tpc(sockfd, "quit");
    close(sockfd);
    // stop_test_server();
}

void test_init_connection_tcp_invalid_ip(void)
{
    init_params_client params = {
        .connection_params = {.ip_version = IPV4, .host = "256.256.256.256", .port = TEST_PORT_TCP}};
    int sockfd = init_connection_tcp(params);
    TEST_ASSERT_EQUAL(-1, sockfd);
}

int main(void)
{
    test_server_t udp_server;
    test_server_t tcp_server;
    start_test_server(
        &udp_server,
        (server_config_t){.mode = SERVER_MODE_UDP, .family = SERVER_FAMILY_IPV4, .port = atoi(TEST_PORT_UDP)});
    start_test_server(
        &tcp_server,
        (server_config_t){.mode = SERVER_MODE_TCP, .family = SERVER_FAMILY_IPV4, .port = atoi(TEST_PORT_TCP)});
    UNITY_BEGIN();
    RUN_TEST(test_setup_socket_udp_ipv4_valid);
    RUN_TEST(test_setup_socket_udp_invalid_ip);
    RUN_TEST(test_sender_udp_invalid_socket);
    RUN_TEST(test_receiver_udp_invalid_socket);
    RUN_TEST(test_setup_socket_tcp_invalid_ip);
    RUN_TEST(test_sender_tcp_invalid_socket);
    RUN_TEST(test_receiver_tcp_invalid_socket);
    RUN_TEST(test_init_connection_udp_valid);
    RUN_TEST(test_init_connection_udp_invalid_ip);
    RUN_TEST(test_init_connection_tcp_valid);
    RUN_TEST(test_init_connection_tcp_invalid_ip);
    int result = UNITY_END();
    stop_test_server(&udp_server);
    stop_test_server(&tcp_server);
    return result;
}
