#include "connection.h"
#include "mock_server.h"
#include "unity.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOCALHOST "127.0.0.1"
#define TCP_TEST_PORT "8081"
#define UDP_TEST_PORT "8082"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_client_tcp_connection(void)
{
    pthread_t server_thread;
    mock_server_args_t args = {.port = 8081, .response_msg = "Hello from server"};

    pthread_create(&server_thread, NULL, mock_tcp_server, &args);
    sleep(1);

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TCP_TEST_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));

    char response[1024];
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello"));
    int bytes_received = client_receive(&ctx, response, sizeof(response));
    TEST_ASSERT_GREATER_THAN(0, bytes_received);
    TEST_ASSERT_EQUAL_STRING("Hello from server", response);

    client_close(&ctx);
    pthread_join(server_thread, NULL);
}

void test_client_udp_connection(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, UDP_TEST_PORT, sizeof(config.port));
    config.protocol = PROTO_UDP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello UDP"));

    client_close(&ctx);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_client_tcp_connection);
    RUN_TEST(test_client_udp_connection);
    return UNITY_END();
}
