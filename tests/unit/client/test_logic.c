#define _POSIX_C_SOURCE 200809L
#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include "logic.h"
#include "shared_state.h"
#include "unity.h"
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_LOG_FILE_SIZE (10 * 1024 * 1024)
#define TEST_LOG_BACKUPS 3
#define TEST_TIMEOUT_SEC 2

void setUp(void)
{
    logger_config_t config = {.log_file_path = "/tmp/test_logic.log",
                              .max_file_size = TEST_LOG_FILE_SIZE,
                              .max_backup_files = TEST_LOG_BACKUPS,
                              .min_level = LOG_DEBUG};
    log_init(&config);

    if (ipc_init("test_logic") != 0)
    {
        TEST_FAIL_MESSAGE("Failed to initialize IPC in setUp");
    }

    shared_data_t* shared_data = get_shared_data();
    memset(shared_data, 0, sizeof(shared_data_t));
    strncpy(shared_data->client_role, HUB, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "HUB001", sizeof(shared_data->client_id) - 1);
    shared_data->should_exit = 0;

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].item_id = i + 1;
        snprintf(shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        shared_data->inventory_item[i].quantity = 50;
    }
}

void tearDown(void)
{
    ipc_cleanup();
    log_close();
}

void test_ipc_initialization(void)
{
    shared_data_t* shared_data = get_shared_data();

    TEST_ASSERT_NOT_NULL(shared_data);
    TEST_ASSERT_EQUAL_STRING("HUB", shared_data->client_role);
    TEST_ASSERT_EQUAL_STRING("HUB001", shared_data->client_id);
    TEST_ASSERT_EQUAL_INT(0, shared_data->should_exit);
}

void test_inventory_operations(void)
{
    inventory_item_t items[QUANTITY_ITEMS];

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        items[i].item_id = i + 1;
        snprintf(items[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        items[i].quantity = 10;
    }

    int result = modify_inventory(items, INVENTORY_ADD);
    TEST_ASSERT_EQUAL_INT(0, result);

    shared_data_t* shared_data = get_shared_data();
    TEST_ASSERT_EQUAL_INT(60, shared_data->inventory_item[0].quantity);

    items[0].quantity = 5;
    result = modify_inventory(items, INVENTORY_REDUCE);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(55, shared_data->inventory_item[0].quantity);
}

void test_message_queue_operations(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));

    strncpy(msg.msg_type, HUB_TO_SERVER__KEEPALIVE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.source_role, HUB, ROLE_SIZE - 1);
    strncpy(msg.source_id, "HUB001", ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    int result = enqueue_pending_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    message_t popped_msg;
    result = pop_pending_message(&popped_msg);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__KEEPALIVE, popped_msg.msg_type);
}

void test_pending_ack_tracking(void)
{
    const char* msg_id = "2025-11-25T10:00:00.000Z";
    const char* msg_type = HUB_TO_SERVER__INVENTORY_UPDATE;
    const char* json = "{\"test\":\"data\"}";

    int result = add_pending_ack(msg_id, msg_type, json);
    TEST_ASSERT_EQUAL_INT(0, result);

    shared_data_t* shared_data = get_shared_data();
    int found = 0;
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active && strcmp(shared_data->pending_acks[i].msg_id, msg_id) == 0)
        {
            found = 1;
            TEST_ASSERT_EQUAL_STRING(msg_type, shared_data->pending_acks[i].msg_type);
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found);

    result = remove_pending_ack(msg_id);
    TEST_ASSERT_EQUAL_INT(0, result);

    found = 0;
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active && strcmp(shared_data->pending_acks[i].msg_id, msg_id) == 0)
        {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, found);
}

void test_inventory_count_query(void)
{
    int count = get_inventory_count(1);
    TEST_ASSERT_EQUAL_INT(50, count);

    count = get_inventory_count(999);
    TEST_ASSERT_EQUAL_INT(-1, count);
}

/**
 * @brief Smoke test for logic_init()
 *
 * This test verifies that logic_init() can start and stop without crashing.
 * It does NOT test the full functionality (threads, timers, etc.) because
 * that would require a real network connection and would run indefinitely.
 *
 * The test:
 * 1. Forks a child process
 * 2. Child sets up a timeout alarm (2 seconds)
 * 3. Child calls logic_init() which will fork again and start threads
 * 4. Alarm triggers SIGALRM after 2 seconds
 * 5. Signal handler sets should_exit flag
 * 6. logic_init() detects flag and cleans up
 * 7. Parent verifies child didn't crash (no segfault)
 */
static void alarm_handler(int sig)
{
    (void)sig;

    shared_data_t* shared_data = get_shared_data();
    if (shared_data)
    {
        shared_data->should_exit = 1;

        sem_t* message_sem = get_message_sem();
        if (message_sem)
        {
            sem_post(message_sem);
        }
    }
}

void test_logic_init_smoke_test(void)
{
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

        if (ipc_init("smoke_test_logic") != 0)
        {
            exit(1);
        }

        client_context ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.sockfd = -1;

        (void)logic_init(&ctx, HUB, "SMOKE_TEST");

        ipc_cleanup();

        exit(0);
    }
    else if (test_pid > 0)
    {
        int status;
        pid_t waited = waitpid(test_pid, &status, 0);

        TEST_ASSERT_EQUAL_INT(test_pid, waited);

        if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            TEST_ASSERT_TRUE(exit_code == 0 || exit_code == 255);
        }
        else if (WIFSIGNALED(status))
        {
            int signal = WTERMSIG(status);

            if (signal == SIGSEGV)
            {
                TEST_FAIL_MESSAGE("logic_init() caused segmentation fault");
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

    RUN_TEST(test_ipc_initialization);
    RUN_TEST(test_inventory_operations);
    RUN_TEST(test_message_queue_operations);
    RUN_TEST(test_pending_ack_tracking);
    RUN_TEST(test_inventory_count_query);

    RUN_TEST(test_logic_init_smoke_test);

    return UNITY_END();
}
