#define _POSIX_C_SOURCE 200809L

#include "logic.h"
#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include "message_handler.h"
#include "shared_state.h"
#include "unity.h"
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../../src/client/logic.c"

#define TEST_LOG_FILE_SIZE (10 * 1024 * 1024)
#define TEST_LOG_BACKUPS 3
#define RANDOM_SAMPLE_COUNT 100
#define TEST_STOCK_BASELINE 50

void setUp(void)
{
    logger_config_t config = {.log_file_path = "/tmp/test_logic_static.log",
                              .max_file_size = TEST_LOG_FILE_SIZE,
                              .max_backup_files = TEST_LOG_BACKUPS,
                              .min_level = LOG_DEBUG};
    log_init(&config);

    load_timer_config();

    if (ipc_init("test_logic_static") != 0)
    {
        TEST_FAIL_MESSAGE("Failed to initialize IPC in setUp");
    }

    shared_data_t* shared_data = get_shared_data();
    memset(shared_data, 0, sizeof(shared_data_t));
    strncpy(shared_data->client_role, HUB, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "HUB_TEST", sizeof(shared_data->client_id) - 1);
    shared_data->should_exit = 0;
    shared_data->inventory_updated = 0;

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].item_id = i + 1;
        snprintf(shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        shared_data->inventory_item[i].quantity = TEST_STOCK_BASELINE;
    }
}

void tearDown(void)
{
    ipc_cleanup();
    log_close();
}

void test_get_random_consume_interval(void)
{
    for (int i = 0; i < RANDOM_SAMPLE_COUNT; i++)
    {
        int interval = get_random_consume_interval();
        
        const timer_config_t* cfg = get_timer_config();
        TEST_ASSERT_TRUE(interval >= cfg->consume_stock_min_ms);
        TEST_ASSERT_TRUE(interval <= cfg->consume_stock_max_ms);
    }
}

void test_get_random_consume_amount(void)
{
    int min_seen = 999;
    int max_seen = 0;

    for (int i = 0; i < RANDOM_SAMPLE_COUNT; i++)
    {
        int amount = get_random_consume_amount();
        
        if (amount < min_seen) min_seen = amount;
        if (amount > max_seen) max_seen = amount;
        
        const timer_config_t* cfg = get_timer_config();
        TEST_ASSERT_TRUE(amount >= cfg->consume_min_amount);
        TEST_ASSERT_TRUE(amount <= cfg->consume_max_amount);
    }
}

void test_do_inventory_update(void)
{
    shared_data_t* shared_data = get_shared_data();

    shared_data->message_count = 0;

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 10 + i * 5;
    }
    sem_post(get_inventory_sem());

    do_inventory_update();

    TEST_ASSERT_TRUE(shared_data->message_count >= 2);
}

void test_do_consume_stock(void)
{
    shared_data_t* shared_data = get_shared_data();

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 100;
    }
    sem_post(get_inventory_sem());

    int initial_total = 0;
    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        initial_total += shared_data->inventory_item[i].quantity;
    }
    sem_post(get_inventory_sem());

    do_consume_stock();

    int final_total = 0;
    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        final_total += shared_data->inventory_item[i].quantity;
    }
    sem_post(get_inventory_sem());

    TEST_ASSERT_TRUE(final_total < initial_total);
}

void test_do_consume_stock_zero_inventory(void)
{
    shared_data_t* shared_data = get_shared_data();

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 0;
    }
    sem_post(get_inventory_sem());

    do_consume_stock();

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, shared_data->inventory_item[i].quantity);
    }
    sem_post(get_inventory_sem());
}

void test_do_check_low_stock_triggers_request(void)
{
    shared_data_t* shared_data = get_shared_data();

    shared_data->message_count = 0;

    sem_wait(get_inventory_sem());
    shared_data->inventory_item[0].quantity = 15;
    shared_data->inventory_item[1].quantity = 10;
    shared_data->inventory_item[2].quantity = 5;
    shared_data->inventory_item[3].quantity = 25;
    shared_data->inventory_item[4].quantity = 30;
    shared_data->inventory_item[5].quantity = 18;
    sem_post(get_inventory_sem());

    int result = do_check_low_stock();

    TEST_ASSERT_EQUAL_INT(0, result);

    TEST_ASSERT_TRUE(shared_data->message_count > 0);
}

void test_do_check_low_stock_critically_low(void)
{
    shared_data_t* shared_data = get_shared_data();

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 3;
    }
    sem_post(get_inventory_sem());

    int result = do_check_low_stock();

    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_do_check_low_stock_sufficient_stock(void)
{
    shared_data_t* shared_data = get_shared_data();

    shared_data->message_count = 0;

    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 50;
    }
    sem_post(get_inventory_sem());

    int result = do_check_low_stock();

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(0, shared_data->message_count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_random_consume_interval);
    RUN_TEST(test_get_random_consume_amount);

    RUN_TEST(test_do_inventory_update);
    RUN_TEST(test_do_consume_stock);
    RUN_TEST(test_do_consume_stock_zero_inventory);
    RUN_TEST(test_do_check_low_stock_triggers_request);
    RUN_TEST(test_do_check_low_stock_critically_low);
    RUN_TEST(test_do_check_low_stock_sufficient_stock);

    return UNITY_END();
}
