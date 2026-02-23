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
    sem_wait(&ready_sem);  // Wait for server to be ready (no more sleep!)

    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, "8081", sizeof(config.port));
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
    strncpy(config.port, "8083", sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));

    // -1 (ECONNRESET) or -2 (graceful close/EOF)
    char response[BUFFER_SIZE];
    TEST_ASSERT_EQUAL(0, client_send(&ctx, "Hello"));
    int result = client_receive(&ctx, response, sizeof(response));
    TEST_ASSERT_TRUE(result == -1 || result == -2);  // Either error is valid

    client_close(&ctx);
    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_udp_connection(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, "8082", sizeof(config.port));
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
    strncpy(config.port, "8084", sizeof(config.port));
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
    strncpy(config.port, "8081", sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    // getaddrinfo should fail with invalid host
    TEST_ASSERT_EQUAL(-1, client_init(&ctx, &config));
}

void test_client_init_connection_refused(void)
{
    client_context ctx;
    client_config config;

    strncpy(config.host, LOCALHOST, sizeof(config.host));
    strncpy(config.port, "59999", sizeof(config.port));  // Port with no server
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
    strncpy(config.port, "8085", sizeof(config.port));
    config.protocol = PROTO_TCP;
    config.ip_version = AF_INET;

    TEST_ASSERT_EQUAL(0, client_init(&ctx, &config));
    TEST_ASSERT_GREATER_OR_EQUAL(0, ctx.sockfd);

    client_close(&ctx);
    TEST_ASSERT_EQUAL(-1, ctx.sockfd);  // Should be set to -1 after close

    pthread_join(server_thread, NULL);
    sem_destroy(&ready_sem);
}

void test_client_close_already_closed(void)
{
    client_context ctx;
    ctx.sockfd = -1;  // Already closed/invalid

    client_close(&ctx);
    TEST_ASSERT_EQUAL(-1, ctx.sockfd);
}

int main(void)
{
    UNITY_BEGIN();

    // TCP tests
    RUN_TEST(test_client_tcp_connection);
    RUN_TEST(test_client_tcp_server_disconnect);

    // UDP tests
    RUN_TEST(test_client_udp_connection);
    RUN_TEST(test_client_udp_send_receive);

    // Error handling tests
    RUN_TEST(test_client_init_invalid_host);
    RUN_TEST(test_client_init_connection_refused);

    // client_close tests
    RUN_TEST(test_client_close_valid_socket);
    RUN_TEST(test_client_close_already_closed);

    return UNITY_END();
}
