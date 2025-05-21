#include "config.h"
#include "inventory.h"
#include "json_manager.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

#define WATER "water"
#define FOOD "food"
#define MEDICINE "medicine"
#define GUNS "guns"
#define AMMO "ammo"
#define TOOLS "tools"
#define SHM_SIZE 1024
#define SHM_KEY 50
#define SEM_KEY 51
#define SEM_NUM 0
#define SEM_SENDER 1

#define WAREHOUSE_MAX 400
#define HUB_MAX 100
#define WAREHOUSE_MIN WAREHOUSE_MAX * 0.2
#define HUB_MIN HUB_MAX * 0.2

void setUp(void)
{
    set_username("test_user");
    set_client_type("warehouse");
    set_shm_path();
    TEST_ASSERT_EQUAL(0, init_shared_memory());
}

void tearDown(void)
{
    cleanup_shared_memory();
}

void test_get_inventory_size(void)
{
    int size = get_inventory_size();
    TEST_ASSERT_EQUAL(6, size);
}

void test_inventory_initialization(void)
{
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);

    // Check that all items are initialized with correct names and zero quantities
    TEST_ASSERT_EQUAL_STRING(WATER, inv[0].item);
    TEST_ASSERT_EQUAL_STRING(FOOD, inv[1].item);
    TEST_ASSERT_EQUAL_STRING(MEDICINE, inv[2].item);
    TEST_ASSERT_EQUAL_STRING(GUNS, inv[3].item);
    TEST_ASSERT_EQUAL_STRING(AMMO, inv[4].item);
    TEST_ASSERT_EQUAL_STRING(TOOLS, inv[5].item);

    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
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
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
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

void test_get_inventory_received(void)
{
    inventory_item* inv = get_inventory_received();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
    }
    free(inv);
}

void test_get_inventory_to_replenish_warehouse(void)
{
    set_client_type("warehouse");
    inventory_item* inv = get_inventory_to_replenish();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(WAREHOUSE_MAX, inv[i].quantity);
    }
    free(inv);
}

void test_get_inventory_to_replenish_hub(void)
{
    set_client_type("hub");
    inventory_item* inv = get_inventory_to_replenish();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(HUB_MAX, inv[i].quantity);
    }
    free(inv);
}

void test_set_inventory_to_send(void)
{
    inventory_item items_to_send[6] = {{WATER, 10}, {FOOD, 20}, {MEDICINE, 30}, {GUNS, 40}, {AMMO, 50}, {TOOLS, 60}};

    // First set some initial inventory
    set_inventory(items_to_send);

    // Then set items to send
    set_inventory_to_send(items_to_send);

    // Check that inventory was reduced
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, inv[i].quantity);
    }
    free(inv);

    // Check that items to send were set
    inventory_item* to_send = get_inventory_to_send();
    TEST_ASSERT_NOT_NULL(to_send);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(items_to_send[i].quantity, to_send[i].quantity);
    }
    free(to_send);
}

void test_set_inventory_warehouse(void)
{
    set_client_type("warehouse");
    inventory_item items[6] = {{WATER, 500}, {FOOD, 500}, {MEDICINE, 500}, {GUNS, 500}, {AMMO, 500}, {TOOLS, 500}};

    set_inventory(items);
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(WAREHOUSE_MAX, inv[i].quantity);
    }
    free(inv);
}

void test_set_inventory_hub(void)
{
    set_client_type("hub");
    inventory_item items[6] = {{WATER, 200}, {FOOD, 200}, {MEDICINE, 200}, {GUNS, 200}, {AMMO, 200}, {TOOLS, 200}};

    set_inventory(items);
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(HUB_MAX, inv[i].quantity);
    }
    free(inv);
}

void test_set_inventory_received(void)
{
    inventory_item items[6] = {{WATER, 10}, {FOOD, 20}, {MEDICINE, 30}, {GUNS, 40}, {AMMO, 50}, {TOOLS, 60}};

    set_inventory_received(items);
    inventory_item* received = get_inventory_received();
    TEST_ASSERT_NOT_NULL(received);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_EQUAL_INT(items[i].quantity, received[i].quantity);
        TEST_ASSERT_EQUAL_STRING(items[i].item, received[i].item);
    }
    free(received);
}

void test_replenish_warehouse(void)
{
    set_client_type("warehouse");
    shared_data* data = get_shared_data();
    data->items[0].quantity = WAREHOUSE_MIN - 1;
    TEST_ASSERT_EQUAL(1, replenish());
}

void test_replenish_hub(void)
{
    set_client_type("hub");
    shared_data* data = get_shared_data();
    data->items[0].quantity = HUB_MIN - 1;
    TEST_ASSERT_EQUAL(1, replenish());
}

void test_hub_username_management(void)
{
    const char* test_username = "test_hub";
    set_hub_username(test_username);
    char* username = get_hub_username();
    TEST_ASSERT_NOT_NULL(username);
    TEST_ASSERT_EQUAL_STRING(test_username, username);
    free(username);
}

void test_warehouse_username_management(void)
{
    const char* test_username = "test_warehouse";
    set_warehouse_username(test_username);
    char* username = get_warehouse_username();
    TEST_ASSERT_NOT_NULL(username);
    TEST_ASSERT_EQUAL_STRING(test_username, username);
    free(username);
}

void test_inventory_consumption(void)
{
    set_client_type("hub");
    // Set initial inventory
    inventory_item items[6] = {{WATER, HUB_MAX}, {FOOD, HUB_MAX}, {MEDICINE, HUB_MAX},
                               {GUNS, HUB_MAX},  {AMMO, HUB_MAX}, {TOOLS, HUB_MAX}};
    set_inventory(items);

    // Test consumption
    TEST_ASSERT_EQUAL(0, inventory_compsumption());

    // Verify inventory was reduced
    inventory_item* inv = get_inventory();
    TEST_ASSERT_NOT_NULL(inv);
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        TEST_ASSERT_LESS_THAN_INT(HUB_MAX, inv[i].quantity);
    }
    free(inv);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_get_inventory_size);
    RUN_TEST(test_inventory_initialization);
    RUN_TEST(test_get_shared_data);
    RUN_TEST(test_get_semaphore_id);
    RUN_TEST(test_get_inventory);
    RUN_TEST(test_get_inventory_to_send);
    RUN_TEST(test_get_inventory_received);
    RUN_TEST(test_get_inventory_to_replenish_warehouse);
    RUN_TEST(test_get_inventory_to_replenish_hub);
    RUN_TEST(test_set_inventory_to_send);
    RUN_TEST(test_set_inventory_warehouse);
    RUN_TEST(test_set_inventory_hub);
    RUN_TEST(test_set_inventory_received);
    RUN_TEST(test_replenish_warehouse);
    RUN_TEST(test_replenish_hub);
    RUN_TEST(test_hub_username_management);
    RUN_TEST(test_warehouse_username_management);
    RUN_TEST(test_inventory_consumption);
    return UNITY_END();
}
