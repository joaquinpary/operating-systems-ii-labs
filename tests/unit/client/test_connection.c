#include "connection.h"
#include "mock_server.h"
#include "unity.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOCALHOST "127.0.0.1"
#define INVALID_HOST "invalid.host.that.does.not.exist.local"
#define TEST_TCP_PORT "8081"
#define TEST_TCP_DISCONNECT_PORT "8083"
#define TEST_UDP_PORT "8082"
#define TEST_UDP_ECHO_PORT "8084"
#define TEST_CLOSE_PORT "8085"
#define TEST_UNUSED_PORT "59999"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_client_tcp_connection(void)
{
    pthread_t server_thread;
    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);

    mock_server_args_t args = {
        .port = 8081,
        .response_msg = "Hello from server",
        .ready_sem = &ready_sem,
        .behavior = MOCK_BEHAVIOR_NORMAL
    };

    pthread_create(&server_thread, NULL, mock_tcp_server, &args);
    sem_wait(&ready_sem);

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_TCP_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));

    char response[BUFFER_SIZE];
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello"));
    int bytes_received = client_receive(&ctx, response, sizeof(response));
    TEST_ASSERT_GREATER_THAN(0, bytes_received);
    TEST_ASSERT_EQUAL_STRING("Hello from server", response);

    client_close(&ctx);
    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_tcp_server_disconnect(void)
{
    pthread_t server_thread;
    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);

    mock_server_args_t args = {
        .port = 8083,
        .response_msg = "",
        .ready_sem = &ready_sem,
        .behavior = MOCK_BEHAVIOR_CLOSE_IMMEDIATE
    };

    pthread_create(&server_thread, NULL, mock_tcp_server, &args);
    sem_wait(&ready_sem);

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_TCP_DISCONNECT_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));

    char response[BUFFER_SIZE];
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello"));
    int result = client_receive(&ctx, response, sizeof(response));
    TEST_ASSERT_TRUE(result == -1 || result == -2);

    client_close(&ctx);
    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_udp_connection(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_UDP_PORT, sizeof(config.port));
    config.protocol = PROTO_UDP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello UDP"));

    client_close(&ctx);
}

void test_client_udp_send_receive(void)
{
    pthread_t server_thread;
    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);

    mock_server_args_t args = {
        .port = 8084,
        .response_msg = "UDP Response",
        .ready_sem = &ready_sem,
        .behavior = MOCK_BEHAVIOR_NORMAL
    };

    pthread_create(&server_thread, NULL, mock_udp_server, &args);
    sem_wait(&ready_sem);

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_UDP_ECHO_PORT, sizeof(config.port));
    config.protocol = PROTO_UDP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello UDP"));

    char response[BUFFER_SIZE];
    int bytes_received = client_receive(&ctx, response, sizeof(response));
    TEST_ASSERT_GREATER_THAN(0, bytes_received);
    TEST_ASSERT_EQUAL_STRING("UDP Response", response);

    client_close(&ctx);
    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_init_invalid_host(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, INVALID_HOST, sizeof(config.host));
    strncpy(config.port, TEST_TCP_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(-1, client_init(&ctx, &config));
}

void test_client_init_connection_refused(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_UNUSED_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(-1, client_init(&ctx, &config));
}

void test_client_close_valid_socket(void)
{
    pthread_t server_thread;
    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);

    mock_server_args_t args = {
        .port = 8085,
        .response_msg = "Test",
        .ready_sem = &ready_sem,
        .behavior = MOCK_BEHAVIOR_CLOSE_IMMEDIATE
    };

    pthread_create(&server_thread, NULL, mock_tcp_server, &args);
    sem_wait(&ready_sem);

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, TEST_CLOSE_PORT, sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));
    TEST_ASSERT_GREATER_OR_EQUAL(0, ctx.sockfd);

    client_close(&ctx);
    TEST_ASSERT_EQUAL(-1, ctx.sockfd);

    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_close_already_closed(void)
{
    client_context ctx;
    ctx.sockfd = -1;

    client_close(&ctx);
    TEST_ASSERT_EQUAL(-1, ctx.sockfd);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_client_tcp_connection);
    RUN_TEST(test_client_tcp_server_disconnect);

    RUN_TEST(test_client_udp_connection);
    RUN_TEST(test_client_udp_send_receive);

    RUN_TEST(test_client_init_invalid_host);
    RUN_TEST(test_client_init_connection_refused);

    RUN_TEST(test_client_close_valid_socket);
    RUN_TEST(test_client_close_already_closed);

    return UNITY_END();
}
