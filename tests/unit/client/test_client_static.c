#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include "message_handler.h"
#include "unity.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../../src/client/client.c"

// Include mock server
#include "mock_server.h"

#define TEST_LOG_FILE_SIZE (10 * 1024 * 1024)
#define TEST_LOG_BACKUPS 3
#define TEST_CONFIG_VALID "/tmp/test_client_valid.conf"
#define TEST_CONFIG_INVALID "/tmp/test_client_invalid.conf"
#define TEST_CONFIG_MISSING "/tmp/test_client_missing.conf"
#define TEST_CONFIG_INCOMPLETE "/tmp/test_client_incomplete.conf"
#define TEST_AUTH_SUCCESS_PORT 9999
#define TEST_AUTH_FAILURE_PORT 9998
#define TEST_SETUP_DELAY_US 100000
#define TEST_TIMEOUT_SEC 2

void setUp(void)
{
    logger_config_t config = {.log_file_path = "/tmp/test_client_static.log",
                              .max_file_size = TEST_LOG_FILE_SIZE,
                              .max_backup_files = TEST_LOG_BACKUPS,
                              .min_level = LOG_DEBUG};
    log_init(&config);
}

void tearDown(void)
{
    remove(TEST_CONFIG_VALID);
    remove(TEST_CONFIG_INVALID);
    remove(TEST_CONFIG_INCOMPLETE);
    log_close();
}

// ==================== HELPER FUNCTIONS ====================

/**
 * @brief Create a test configuration file
 * @param path File path
 * @param content Content to write
 */
static void create_test_config(const char* path, const char* content)
{
    FILE* f = fopen(path, "w");
    if (f)
    {
        fputs(content, f);
        fclose(f);
    }
}

void test_parse_conf_valid_tcp_config(void)
{
    const char* config_content = "host = localhost\n"
                                 "port = 8080\n"
                                 "protocol = tcp\n"
                                 "ipversion = v4\n"
                                 "type = HUB\n"
                                 "username = test_user\n"
                                 "password = test_pass\n";

    create_test_config(TEST_CONFIG_VALID, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_VALID, &config, &creds);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("localhost", config.host);
    TEST_ASSERT_EQUAL_STRING("8080", config.port);
    TEST_ASSERT_EQUAL_INT(PROTO_TCP, config.protocol);
    TEST_ASSERT_EQUAL_INT(AF_INET, config.ip_version);
    TEST_ASSERT_EQUAL_STRING("HUB", creds.type);
    TEST_ASSERT_EQUAL_STRING("test_user", creds.username);
    TEST_ASSERT_EQUAL_STRING("test_pass", creds.password);
}

void test_parse_conf_valid_udp_config(void)
{
    const char* config_content = "host=192.168.1.100\n"
                                 "port=9090\n"
                                 "protocol=udp\n"
                                 "ipversion=v6\n"
                                 "type=WAREHOUSE\n"
                                 "username=warehouse_01\n"
                                 "password=securepass123\n";

    create_test_config(TEST_CONFIG_VALID, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_VALID, &config, &creds);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", config.host);
    TEST_ASSERT_EQUAL_STRING("9090", config.port);
    TEST_ASSERT_EQUAL_INT(PROTO_UDP, config.protocol);
    TEST_ASSERT_EQUAL_INT(AF_INET6, config.ip_version);
    TEST_ASSERT_EQUAL_STRING("WAREHOUSE", creds.type);
    TEST_ASSERT_EQUAL_STRING("warehouse_01", creds.username);
    TEST_ASSERT_EQUAL_STRING("securepass123", creds.password);
}

void test_parse_conf_with_spaces_and_tabs(void)
{
    const char* config_content = "  host   =   localhost   \n"
                                 "\tport\t=\t8080\t\n"
                                 " protocol = tcp \n"
                                 "ipversion = v4\n"
                                 "type = HUB\n"
                                 "username = test_user\n"
                                 "password = test_pass\n";

    create_test_config(TEST_CONFIG_VALID, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_VALID, &config, &creds);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("localhost", config.host);
    TEST_ASSERT_EQUAL_STRING("8080", config.port);
}

void test_parse_conf_with_comments_and_empty_lines(void)
{
    const char* config_content = "# This is a comment\n"
                                 "host = localhost\n"
                                 "\n"
                                 "port = 8080\n"
                                 "protocol = tcp\n"
                                 "some random line\n"
                                 "ipversion = v4\n"
                                 "type = HUB\n"
                                 "username = test_user\n"
                                 "password = test_pass\n";

    create_test_config(TEST_CONFIG_VALID, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_VALID, &config, &creds);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("localhost", config.host);
}

void test_parse_conf_missing_host(void)
{
    const char* config_content = "port = 8080\n"
                                 "protocol = tcp\n"
                                 "ipversion = v4\n"
                                 "type = HUB\n"
                                 "username = test_user\n"
                                 "password = test_pass\n";

    create_test_config(TEST_CONFIG_INCOMPLETE, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_INCOMPLETE, &config, &creds);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_conf_missing_credentials(void)
{
    const char* config_content = "host = localhost\n"
                                 "port = 8080\n"
                                 "protocol = tcp\n"
                                 "ipversion = v4\n"
                                 "type = HUB\n";

    create_test_config(TEST_CONFIG_INCOMPLETE, config_content);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_INCOMPLETE, &config, &creds);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_conf_file_not_found(void)
{
    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_MISSING, &config, &creds);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_conf_ipversion_variants(void)
{
    const char* config_v6 = "host = localhost\n"
                            "port = 8080\n"
                            "protocol = tcp\n"
                            "ipversion = ipv6\n"
                            "type = HUB\n"
                            "username = user\n"
                            "password = pass\n";

    create_test_config(TEST_CONFIG_VALID, config_v6);

    client_config config;
    client_credentials creds;

    int result = parse_conf(TEST_CONFIG_VALID, &config, &creds);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(AF_INET6, config.ip_version);

    const char* config_unspec = "host = localhost\n"
                                "port = 8080\n"
                                "protocol = tcp\n"
                                "ipversion = auto\n"
                                "type = HUB\n"
                                "username = user\n"
                                "password = pass\n";

    create_test_config(TEST_CONFIG_VALID, config_unspec);

    result = parse_conf(TEST_CONFIG_VALID, &config, &creds);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(AF_UNSPEC, config.ip_version);
}

void test_authenticate_success(void)
{
    message_t response_msg;
    if (create_auth_response_message(&response_msg, "HUB", "test_user", 200) != 0)
    {
        TEST_FAIL_MESSAGE("Failed to create AUTH_RESPONSE message");
        return;
    }

    char success_response[BUFFER_SIZE];
    if (serialize_message_to_json(&response_msg, success_response) != 0)
    {
        TEST_FAIL_MESSAGE("Failed to serialize AUTH_RESPONSE message");
        return;
    }

    mock_server_args_t server_args = {
        .port = TEST_AUTH_SUCCESS_PORT,
        .response_msg = success_response,
        .ready_sem = NULL,
        .behavior = MOCK_BEHAVIOR_NORMAL
    };

    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);
    server_args.ready_sem = &ready_sem;

    pthread_t server_thread;
    pthread_create(&server_thread, NULL, mock_tcp_server, &server_args);

    sem_wait(&ready_sem);
    sem_destroy(&ready_sem);

    usleep(TEST_SETUP_DELAY_US);

    client_config config = {
        .host = "localhost",
        .port = "9999",
        .protocol = PROTO_TCP,
        .ip_version = AF_INET
    };

    client_context ctx;
    int init_result = client_init(&ctx, &config);
    if (init_result != 0)
    {
        pthread_cancel(server_thread);
        pthread_join(server_thread, NULL);
        TEST_FAIL_MESSAGE("Failed to initialize client");
        return;
    }

    client_credentials creds = {
        .type = "HUB",
        .username = "test_user",
        .password = "test_pass"
    };

    int result = authenticate(&ctx, &creds);

    client_close(&ctx);
    pthread_join(server_thread, NULL);

    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_authenticate_failure(void)
{
    message_t response_msg;
    if (create_auth_response_message(&response_msg, "HUB", "test_user", 401) != 0)
    {
        TEST_FAIL_MESSAGE("Failed to create AUTH_RESPONSE message");
        return;
    }

    char failure_response[BUFFER_SIZE];
    if (serialize_message_to_json(&response_msg, failure_response) != 0)
    {
        TEST_FAIL_MESSAGE("Failed to serialize AUTH_RESPONSE message");
        return;
    }

    mock_server_args_t server_args = {
        .port = TEST_AUTH_FAILURE_PORT,
        .response_msg = failure_response,
        .ready_sem = NULL,
        .behavior = MOCK_BEHAVIOR_NORMAL
    };

    sem_t ready_sem;
    sem_init(&ready_sem, 0, 0);
    server_args.ready_sem = &ready_sem;

    pthread_t server_thread;
    pthread_create(&server_thread, NULL, mock_tcp_server, &server_args);

    sem_wait(&ready_sem);
    sem_destroy(&ready_sem);

    usleep(TEST_SETUP_DELAY_US);

    client_config config = {
        .host = "localhost",
        .port = "9998",
        .protocol = PROTO_TCP,
        .ip_version = AF_INET
    };

    client_context ctx;
    int init_result = client_init(&ctx, &config);
    if (init_result != 0)
    {
        pthread_cancel(server_thread);
        pthread_join(server_thread, NULL);
        TEST_FAIL_MESSAGE("Failed to initialize client");
        return;
    }

    client_credentials creds = {
        .type = "HUB",
        .username = "test_user",
        .password = "wrong_pass"
    };

    int result = authenticate(&ctx, &creds);

    client_close(&ctx);
    pthread_join(server_thread, NULL);

    TEST_ASSERT_EQUAL_INT(-1, result);
}

static void alarm_handler(int sig)
{
    (void)sig;
}

void test_run_client_smoke_test(void)
{
    const char* config_content = "host = 192.0.2.1\n" // TEST-NET-1 (non-routable)
                                 "port = 9999\n"
                                 "protocol = tcp\n"
                                 "ipversion = v4\n"
                                 "type = HUB\n"
                                 "username = smoke_test\n"
                                 "password = test\n";

    create_test_config(TEST_CONFIG_VALID, config_content);

    pid_t test_pid = fork();

    if (test_pid == 0)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = alarm_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGALRM, &sa, NULL);

        alarm(TEST_TIMEOUT_SEC);

        int result = run_client(TEST_CONFIG_VALID);
        exit(result == 0 ? 0 : 1);
    }
    else if (test_pid > 0)
    {
        int status;
        pid_t waited = waitpid(test_pid, &status, 0);

        TEST_ASSERT_EQUAL_INT(test_pid, waited);

        if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            TEST_ASSERT_TRUE(exit_code == 0 || exit_code == 1);
        }
        else if (WIFSIGNALED(status))
        {
            int signal = WTERMSIG(status);

            if (signal == SIGSEGV)
            {
                TEST_FAIL_MESSAGE("run_client() caused segmentation fault");
            }
            else
            {
                char msg[100];
                snprintf(msg, sizeof(msg), "Unexpected signal: %d", signal);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
    else
    {
        TEST_FAIL_MESSAGE("Fork failed in smoke test");
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_conf_valid_tcp_config);
    RUN_TEST(test_parse_conf_valid_udp_config);
    RUN_TEST(test_parse_conf_with_spaces_and_tabs);
    RUN_TEST(test_parse_conf_with_comments_and_empty_lines);
    RUN_TEST(test_parse_conf_missing_host);
    RUN_TEST(test_parse_conf_missing_credentials);
    RUN_TEST(test_parse_conf_file_not_found);
    RUN_TEST(test_parse_conf_ipversion_variants);

    RUN_TEST(test_authenticate_success);
    RUN_TEST(test_authenticate_failure);

    RUN_TEST(test_run_client_smoke_test);

    return UNITY_END();
}
