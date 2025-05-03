#include "config.h"
#include "json_manager.h"
#include "shared_state.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

void setUp(void)
{
    set_client_id("test_client");
    set_client_type("warehouse");
    TEST_ASSERT_EQUAL(0, init_shared_memory());
}

void tearDown(void)
{
}

void test_get_inventory_size(void)
{
    int size = get_inventory_size();
    TEST_ASSERT_EQUAL(6, size);
}

void test_inventory_initialization_warehouse(void)
{
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(100, inv[i].quantity);
    }
    free(inv);
}

void test_get_shared_data(void)
{
    shared_data* data = get_shared_data();
    TEST_ASSERT_NOT_NULL(data);
}

void test_get_semaphore_id(void)
{
    int semid = get_semaphore_id();
    TEST_ASSERT_NOT_EQUAL(-1, semid);
}

void test_get_inventory(void)
{
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(100, inv[i].quantity);
    }
    free(inv);
}
void test_get_inventory_to_send(void)
{
    inventory_item* inv = get_inventory_to_send();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
    }
    free(inv);
}

void test_get_inventory_to_replenish(void)
{
    inventory_item* inv = get_inventory_to_replenish();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
    }
    free(inv);
}

void test_set_inventory_to_send(void)
{
    inventory_item items_to_send[ITEMS] = {{"item1", 10}, {"item2", 20}, {"item3", 30},
                                           {"item4", 40}, {"item5", 50}, {"item6", 60}};
    set_inventory_to_send(items_to_send);
    inventory_item* inv = get_inventory();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(100 - items_to_send[i].quantity, inv[i].quantity);
    }
    free(inv);
}

void test_set_inventory(void)
{
    inventory_item items[ITEMS] = {{"item1", 10}, {"item2", 20}, {"item3", 30},
                                   {"item4", 40}, {"item5", 50}, {"item6", 60}};
    set_inventory(items);
    inventory_item* inv = get_inventory();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_LESS_OR_EQUAL_INT(100, inv[i].quantity);
    }
    free(inv);
}
void test_replenish_returns_0_when_no_replenish_needed(void)
{
    TEST_ASSERT_EQUAL(0, replenish());
}

void test_replenish_returns_1_when_some_item_below_min(void)
{
    shared_data* data = get_shared_data();
    data->items[0].quantity = 10;
    TEST_ASSERT_EQUAL(1, replenish());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_get_inventory_size);
    RUN_TEST(test_inventory_initialization_warehouse);
    RUN_TEST(test_get_shared_data);
    RUN_TEST(test_get_semaphore_id);
    RUN_TEST(test_get_inventory);
    RUN_TEST(test_get_inventory_to_send);
    RUN_TEST(test_get_inventory_to_replenish);
    RUN_TEST(test_set_inventory_to_send);
    RUN_TEST(test_set_inventory);
    RUN_TEST(test_replenish_returns_0_when_no_replenish_needed);
    RUN_TEST(test_replenish_returns_1_when_some_item_below_min);
    return UNITY_END();
}