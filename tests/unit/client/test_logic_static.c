#define _POSIX_C_SOURCE 200809L

// First include all necessary headers BEFORE including logic.c
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

void setUp(void)
{
    // Initialize logger for tests
    logger_config_t config = {.log_file_path = "/tmp/test_logic_static.log",
                              .max_file_size = 10 * 1024 * 1024,
                              .max_backup_files = 3,
                              .min_level = LOG_DEBUG};
    log_init(&config);

    load_timer_config();

    // Initialize IPC for shared state
    if (ipc_init("test_logic_static") != 0)
    {
        fprintf(stderr, "Failed to initialize IPC in setUp\n");
    }

    // Reset shared data
    shared_data_t* shared_data = get_shared_data();
    memset(shared_data, 0, sizeof(shared_data_t));
    strncpy(shared_data->client_role, HUB, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "HUB_TEST", sizeof(shared_data->client_id) - 1);
    shared_data->should_exit = 0;
    shared_data->inventory_updated = 0;

    // Initialize inventory with medium stock levels
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].item_id = i + 1;
        snprintf(shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        shared_data->inventory_item[i].quantity = 50; // Medium stock
    }
}

void tearDown(void)
{
    ipc_cleanup();
    log_close();
}

// ==================== UNIT TESTS FOR STATIC FUNCTIONS ====================

void test_get_random_consume_interval(void)
{
    for (int i = 0; i < 100; i++)
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

    // Call the static function multiple times
    for (int i = 0; i < 100; i++)
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

    // Clear message queue
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

    // Set up inventory with known quantities
    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 100;
    }
    sem_post(get_inventory_sem());

    // Record initial quantities
    int initial_total = 0;
    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        initial_total += shared_data->inventory_item[i].quantity;
    }
    sem_post(get_inventory_sem());

    // Call the static function
    do_consume_stock();

    // Verify quantities decreased
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

    // Call the static function (should not crash)
    do_consume_stock();

    // Verify quantities remain zero
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

    // Clear message queue
    shared_data->message_count = 0;

    // Set some items below low stock threshold (20)
    sem_wait(get_inventory_sem());
    shared_data->inventory_item[0].quantity = 15; // Below threshold
    shared_data->inventory_item[1].quantity = 10; // Below threshold
    shared_data->inventory_item[2].quantity = 5;  // Below threshold
    shared_data->inventory_item[3].quantity = 25; // Above threshold
    shared_data->inventory_item[4].quantity = 30; // Above threshold
    shared_data->inventory_item[5].quantity = 18; // Below threshold
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

    // Clear message queue
    shared_data->message_count = 0;

    // Set all items above threshold
    sem_wait(get_inventory_sem());
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].quantity = 50;
    }
    sem_post(get_inventory_sem());

    // Call the static function
    int result = do_check_low_stock();

    // Should return 0 (not critically low)
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should not have enqueued messages (no low stock)
    TEST_ASSERT_EQUAL_INT(0, shared_data->message_count);
}

int main(void)
{
    UNITY_BEGIN();

    // Tests for static helper functions
    RUN_TEST(test_get_random_consume_interval);
    RUN_TEST(test_get_random_consume_amount);

    // Tests for static business logic functions
    RUN_TEST(test_do_inventory_update);
    RUN_TEST(test_do_consume_stock);
    RUN_TEST(test_do_consume_stock_zero_inventory);
    RUN_TEST(test_do_check_low_stock_triggers_request);
    RUN_TEST(test_do_check_low_stock_critically_low);
    RUN_TEST(test_do_check_low_stock_sufficient_stock);

    return UNITY_END();
}
