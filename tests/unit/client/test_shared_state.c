#include "shared_state.h"
#include "json_manager.h"
#include "unity.h"
#include <string.h>
#include <time.h>
#include <unistd.h>

void setUp(void)
{
    // Initialize IPC for each test
    ipc_init("test_client");
}

void tearDown(void)
{
    // Cleanup IPC after each test
    ipc_cleanup();
}

void test_enqueue_and_pop_message(void)
{
    message_t msg_in, msg_out;

    // Create a test message
    strncpy(msg_in.msg_type, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, sizeof(msg_in.msg_type) - 1);
    strncpy(msg_in.timestamp, "2025-12-09T20:00:00Z", sizeof(msg_in.timestamp) - 1);
    strncpy(msg_in.source_role, "WAREHOUSE", sizeof(msg_in.source_role) - 1);
    strncpy(msg_in.source_id, "client_0001", sizeof(msg_in.source_id) - 1);
    strncpy(msg_in.target_role, "SERVER", sizeof(msg_in.target_role) - 1);
    strncpy(msg_in.target_id, "SERVER", sizeof(msg_in.target_id) - 1);

    // Enqueue message
    TEST_ASSERT_EQUAL(0, enqueue_pending_message(&msg_in));
    TEST_ASSERT_EQUAL(1, has_pending_messages());

    // Pop message
    TEST_ASSERT_EQUAL(0, pop_pending_message(&msg_out));
    TEST_ASSERT_EQUAL(0, has_pending_messages());

    // Verify message content
    TEST_ASSERT_EQUAL_STRING(msg_in.msg_type, msg_out.msg_type);
    TEST_ASSERT_EQUAL_STRING(msg_in.timestamp, msg_out.timestamp);
    TEST_ASSERT_EQUAL_STRING(msg_in.source_id, msg_out.source_id);
}

void test_message_queue_full(void)
{
    message_t msg;
    strncpy(msg.msg_type, "TEST_MESSAGE", sizeof(msg.msg_type) - 1);
    strncpy(msg.timestamp, "2025-12-09T20:00:00Z", sizeof(msg.timestamp) - 1);

    // Fill queue (max 10 messages)
    for (int i = 0; i < 10; i++)
    {
        TEST_ASSERT_EQUAL(0, enqueue_pending_message(&msg));
    }

    // Try to enqueue 11th message - should fail
    TEST_ASSERT_EQUAL(-1, enqueue_pending_message(&msg));
}

void test_message_queue_fifo_order(void)
{
    message_t msg_in[3], msg_out;

    // Enqueue 3 messages with different timestamps
    for (int i = 0; i < 3; i++)
    {
        snprintf(msg_in[i].timestamp, sizeof(msg_in[i].timestamp), "2025-12-09T20:00:0%dZ", i);
        snprintf(msg_in[i].msg_type, sizeof(msg_in[i].msg_type), "TEST_MSG_%d", i);
        TEST_ASSERT_EQUAL(0, enqueue_pending_message(&msg_in[i]));
    }

    // Pop and verify FIFO order
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL(0, pop_pending_message(&msg_out));
        TEST_ASSERT_EQUAL_STRING(msg_in[i].timestamp, msg_out.timestamp);
        TEST_ASSERT_EQUAL_STRING(msg_in[i].msg_type, msg_out.msg_type);
    }

    TEST_ASSERT_EQUAL(0, has_pending_messages());
}

void test_enqueue_pending_message_json(void)
{
    const char* json_msg = "{\"msg_type\":\"TEST\",\"timestamp\":\"2025-12-09T20:00:00Z\"}";
    message_t msg_out;

    // Enqueue JSON directly
    TEST_ASSERT_EQUAL(0, enqueue_pending_message_json(json_msg));
    TEST_ASSERT_EQUAL(1, has_pending_messages());

    // Pop and verify
    TEST_ASSERT_EQUAL(0, pop_pending_message(&msg_out));
    TEST_ASSERT_EQUAL_STRING("TEST", msg_out.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-12-09T20:00:00Z", msg_out.timestamp);
}

void test_pop_from_empty_queue(void)
{
    message_t msg;

    // Pop from empty queue should fail
    TEST_ASSERT_EQUAL(-1, pop_pending_message(&msg));
    TEST_ASSERT_EQUAL(0, has_pending_messages());
}

void test_add_and_remove_pending_ack(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type = "WAREHOUSE_TO_SERVER__INVENTORY_UPDATE";
    const char* json_msg = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__INVENTORY_UPDATE\"}";

    // Add pending ACK
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    // Remove pending ACK
    TEST_ASSERT_EQUAL(0, remove_pending_ack(msg_id));

    // Try to remove again - should fail (not found)
    TEST_ASSERT_EQUAL(-1, remove_pending_ack(msg_id));
}

void test_add_duplicate_ack_updates_timestamp(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type = WAREHOUSE_TO_SERVER__INVENTORY_UPDATE;
    const char* json_msg = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__INVENTORY_UPDATE\"}";

    // Add ACK first time
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    // Simulate retry by incrementing retry_count manually
    shared_data_t* shared_data = get_shared_data();
    sem_t* inventory_sem = get_inventory_sem();

    sem_wait(inventory_sem);
    shared_data->pending_acks[0].retry_count = 1;
    time_t first_time = shared_data->pending_acks[0].send_time;
    sem_post(inventory_sem);

    sleep(1); // Wait 1 second

    // Add same msg_id + msg_type again (retransmission)
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    // Verify timestamp was updated but retry_count unchanged
    sem_wait(inventory_sem);
    TEST_ASSERT_EQUAL(1, shared_data->pending_acks[0].retry_count);
    TEST_ASSERT_GREATER_THAN(first_time, shared_data->pending_acks[0].send_time);
    sem_post(inventory_sem);
}

void test_add_different_msg_type_same_timestamp(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type_1 = WAREHOUSE_TO_SERVER__INVENTORY_UPDATE;
    const char* msg_type_2 = WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE;
    const char* json_msg_1 = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__INVENTORY_UPDATE\"}";
    const char* json_msg_2 = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE\"}";

    // Add two different messages with same timestamp but different types
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type_1, json_msg_1));
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type_2, json_msg_2));

    // Verify both are tracked separately
    shared_data_t* shared_data = get_shared_data();
    sem_t* inventory_sem = get_inventory_sem();

    sem_wait(inventory_sem);
    int active_count = 0;
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active)
        {
            active_count++;
        }
    }
    TEST_ASSERT_EQUAL(2, active_count);
    sem_post(inventory_sem);
}

void test_max_pending_acks(void)
{
    char msg_id[64];
    const char* msg_type = "TEST_MESSAGE";
    const char* json_msg = "{\"msg_type\":\"TEST_MESSAGE\"}";

    // Fill all 10 slots
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        snprintf(msg_id, sizeof(msg_id), "2025-12-09T20:00:%02dZ", i);
        TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));
    }

    // Try to add 11th - should fail
    snprintf(msg_id, sizeof(msg_id), "2025-12-09T20:00:99Z");
    TEST_ASSERT_EQUAL(-1, add_pending_ack(msg_id, msg_type, json_msg));
}

void test_check_ack_timeouts_no_timeout(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type = "TEST_MESSAGE";
    const char* json_msg = "{\"msg_type\":\"TEST_MESSAGE\"}";

    // Add ACK just now
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    // Check immediately - no timeout yet (ACK_TIMEOUT_SECONDS = 5)
    int result = check_ack_timeouts();
    TEST_ASSERT_EQUAL(0, result);
}

void test_check_ack_timeouts_with_timeout(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type = "TEST_MESSAGE";
    const char* json_msg = "{\"msg_type\":\"TEST_MESSAGE\"}";

    // Add ACK
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    // Manually set send_time to past (simulate timeout)
    shared_data_t* shared_data = get_shared_data();
    sem_t* inventory_sem = get_inventory_sem();

    sem_wait(inventory_sem);
    shared_data->pending_acks[0].send_time = time(NULL) - (ACK_TIMEOUT_SECONDS + 1);
    sem_post(inventory_sem);

    // Check for timeout - should detect and increment retry
    int result = check_ack_timeouts();
    TEST_ASSERT_EQUAL(1, result); // Needs retransmission

    // Verify retry_count incremented
    sem_wait(inventory_sem);
    TEST_ASSERT_EQUAL(1, shared_data->pending_acks[0].retry_count);
    sem_post(inventory_sem);
}

void test_check_ack_timeouts_max_retries(void)
{
    const char* msg_id = "2025-12-09T20:00:00Z";
    const char* msg_type = "TEST_MESSAGE";
    const char* json_msg = "{\"msg_type\":\"TEST_MESSAGE\"}";

    // Add ACK
    TEST_ASSERT_EQUAL(0, add_pending_ack(msg_id, msg_type, json_msg));

    shared_data_t* shared_data = get_shared_data();
    sem_t* inventory_sem = get_inventory_sem();

    // Simulate MAX_RETRIES-1 attempts
    for (int i = 0; i < MAX_RETRIES - 1; i++)
    {
        sem_wait(inventory_sem);
        shared_data->pending_acks[0].send_time = time(NULL) - (ACK_TIMEOUT_SECONDS + 1);
        sem_post(inventory_sem);

        int result = check_ack_timeouts();
        TEST_ASSERT_EQUAL(1, result); // Needs retransmission
    }

    // Final timeout - should trigger disconnect
    sem_wait(inventory_sem);
    shared_data->pending_acks[0].send_time = time(NULL) - (ACK_TIMEOUT_SECONDS + 1);
    sem_post(inventory_sem);

    int result = check_ack_timeouts();
    TEST_ASSERT_EQUAL(-1, result); // Max retries exceeded

    // Verify flag set
    sem_wait(inventory_sem);
    TEST_ASSERT_EQUAL(1, shared_data->ack_timeout_occurred);
    sem_post(inventory_sem);
}

void test_get_shared_data(void)
{
    shared_data_t* data = get_shared_data();
    TEST_ASSERT_NOT_NULL(data);

    // Test writing and reading
    strncpy(data->client_role, "WAREHOUSE", sizeof(data->client_role) - 1);
    strncpy(data->client_id, "client_0001", sizeof(data->client_id) - 1);

    TEST_ASSERT_EQUAL_STRING("WAREHOUSE", data->client_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", data->client_id);
}

void test_get_semaphores(void)
{
    sem_t* inventory_sem = get_inventory_sem();
    sem_t* message_sem = get_message_sem();

    TEST_ASSERT_NOT_NULL(inventory_sem);
    TEST_ASSERT_NOT_NULL(message_sem);

    // Test semaphore operations
    TEST_ASSERT_EQUAL(0, sem_wait(inventory_sem));
    TEST_ASSERT_EQUAL(0, sem_post(inventory_sem));
}

void test_inventory_initialization(void)
{
    // Verify inventory is initialized with 6 items in correct order
    shared_data_t* shared_data = get_shared_data();

    const char* expected_names[] = {"FOOD", "WATER", "MEDICINE", "TOOLS", "GUNS", "AMMO"};

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        TEST_ASSERT_EQUAL(i + 1, shared_data->inventory_item[i].item_id);
        TEST_ASSERT_EQUAL_STRING(expected_names[i], shared_data->inventory_item[i].item_name);
        TEST_ASSERT_EQUAL(0, shared_data->inventory_item[i].quantity); // Start at 0
    }
}

void test_modify_inventory_add(void)
{
    // Create items to add
    inventory_item_t items_to_add[QUANTITY_ITEMS] = {
        {1, "FOOD", 50},     {2, "WATER", 30},    {3, "MEDICINE", 20},
        {4, "TOOLS", 10},    {5, "GUNS", 15},     {6, "AMMO", 100}
    };

    // Add items
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_add, INVENTORY_ADD));

    // Verify quantities
    TEST_ASSERT_EQUAL(50, get_inventory_count(1));
    TEST_ASSERT_EQUAL(30, get_inventory_count(2));
    TEST_ASSERT_EQUAL(20, get_inventory_count(3));
    TEST_ASSERT_EQUAL(10, get_inventory_count(4));
    TEST_ASSERT_EQUAL(15, get_inventory_count(5));
    TEST_ASSERT_EQUAL(100, get_inventory_count(6));

    // Verify inventory_updated flag is set
    shared_data_t* shared_data = get_shared_data();
    TEST_ASSERT_EQUAL(1, shared_data->inventory_updated);
}

void test_modify_inventory_add_accumulates(void)
{
    // First addition
    inventory_item_t items1[QUANTITY_ITEMS] = {
        {1, "FOOD", 50},     {2, "WATER", 50},    {3, "MEDICINE", 50},
        {4, "TOOLS", 50},    {5, "GUNS", 50},     {6, "AMMO", 50}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items1, INVENTORY_ADD));

    // Second addition (should accumulate)
    inventory_item_t items2[QUANTITY_ITEMS] = {
        {1, "FOOD", 25},     {2, "WATER", 25},    {3, "MEDICINE", 25},
        {4, "TOOLS", 25},    {5, "GUNS", 25},     {6, "AMMO", 25}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items2, INVENTORY_ADD));

    // Verify accumulated quantities (50 + 25 = 75)
    for (int i = 1; i <= QUANTITY_ITEMS; i++)
    {
        TEST_ASSERT_EQUAL(75, get_inventory_count(i));
    }
}

void test_modify_inventory_reduce(void)
{
    // First, add inventory
    inventory_item_t items_to_add[QUANTITY_ITEMS] = {
        {1, "FOOD", 100},    {2, "WATER", 100},   {3, "MEDICINE", 100},
        {4, "TOOLS", 100},   {5, "GUNS", 100},    {6, "AMMO", 100}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_add, INVENTORY_ADD));

    // Then reduce
    inventory_item_t items_to_reduce[QUANTITY_ITEMS] = {
        {1, "FOOD", 30},     {2, "WATER", 20},    {3, "MEDICINE", 10},
        {4, "TOOLS", 5},     {5, "GUNS", 50},     {6, "AMMO", 75}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_reduce, INVENTORY_REDUCE));

    // Verify remaining quantities
    TEST_ASSERT_EQUAL(70, get_inventory_count(1));   // 100 - 30
    TEST_ASSERT_EQUAL(80, get_inventory_count(2));   // 100 - 20
    TEST_ASSERT_EQUAL(90, get_inventory_count(3));   // 100 - 10
    TEST_ASSERT_EQUAL(95, get_inventory_count(4));   // 100 - 5
    TEST_ASSERT_EQUAL(50, get_inventory_count(5));   // 100 - 50
    TEST_ASSERT_EQUAL(25, get_inventory_count(6));   // 100 - 75
}

void test_modify_inventory_reduce_partial_depletion(void)
{
    inventory_item_t items_to_add[QUANTITY_ITEMS] = {
        {1, "FOOD", 20},     {2, "WATER", 10},    {3, "MEDICINE", 5},
        {4, "TOOLS", 0},     {5, "GUNS", 100},    {6, "AMMO", 50}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_add, INVENTORY_ADD));

    inventory_item_t items_to_reduce[QUANTITY_ITEMS] = {
        {1, "FOOD", 50},     // Want 50, have 20
        {2, "WATER", 100},   // Want 100, have 10
        {3, "MEDICINE", 10}, // Want 10, have 5
        {4, "TOOLS", 10},    // Want 10, have 0
        {5, "GUNS", 50},     // Want 50, have 100 - OK
        {6, "AMMO", 50}      // Want 50, have 50 - exactly
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_reduce, INVENTORY_REDUCE));

    TEST_ASSERT_EQUAL(0, get_inventory_count(1));
    TEST_ASSERT_EQUAL(0, get_inventory_count(2));
    TEST_ASSERT_EQUAL(0, get_inventory_count(3));
    TEST_ASSERT_EQUAL(0, get_inventory_count(4));
    TEST_ASSERT_EQUAL(50, get_inventory_count(5));
    TEST_ASSERT_EQUAL(0, get_inventory_count(6));
}

void test_modify_inventory_reduce_zero_quantity(void)
{
    // Add inventory
    inventory_item_t items_to_add[QUANTITY_ITEMS] = {
        {1, "FOOD", 100},    {2, "WATER", 100},   {3, "MEDICINE", 100},
        {4, "TOOLS", 100},   {5, "GUNS", 100},    {6, "AMMO", 100}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_to_add, INVENTORY_ADD));

    // Reduce with zero quantities (no change)
    inventory_item_t items_zero[QUANTITY_ITEMS] = {
        {1, "FOOD", 0},      {2, "WATER", 0},     {3, "MEDICINE", 0},
        {4, "TOOLS", 0},     {5, "GUNS", 0},      {6, "AMMO", 0}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items_zero, INVENTORY_REDUCE));

    // Verify no change
    for (int i = 1; i <= QUANTITY_ITEMS; i++)
    {
        TEST_ASSERT_EQUAL(100, get_inventory_count(i));
    }
}

void test_modify_inventory_null_items(void)
{
    // NULL items should fail
    TEST_ASSERT_EQUAL(-1, modify_inventory(NULL, INVENTORY_ADD));
    TEST_ASSERT_EQUAL(-1, modify_inventory(NULL, INVENTORY_REDUCE));
}

void test_get_inventory_count_valid(void)
{
    // Add known quantities
    inventory_item_t items[QUANTITY_ITEMS] = {
        {1, "FOOD", 10},     {2, "WATER", 20},    {3, "MEDICINE", 30},
        {4, "TOOLS", 40},    {5, "GUNS", 50},     {6, "AMMO", 60}
    };
    TEST_ASSERT_EQUAL(0, modify_inventory(items, INVENTORY_ADD));

    // Query each item
    TEST_ASSERT_EQUAL(10, get_inventory_count(1));
    TEST_ASSERT_EQUAL(20, get_inventory_count(2));
    TEST_ASSERT_EQUAL(30, get_inventory_count(3));
    TEST_ASSERT_EQUAL(40, get_inventory_count(4));
    TEST_ASSERT_EQUAL(50, get_inventory_count(5));
    TEST_ASSERT_EQUAL(60, get_inventory_count(6));
}

void test_get_inventory_count_invalid_id(void)
{
    // Invalid item IDs should return -1
    TEST_ASSERT_EQUAL(-1, get_inventory_count(0));
    TEST_ASSERT_EQUAL(-1, get_inventory_count(-1));
    TEST_ASSERT_EQUAL(-1, get_inventory_count(7));
    TEST_ASSERT_EQUAL(-1, get_inventory_count(100));
}

int main(void)
{
    UNITY_BEGIN();

    // Message queue tests
    RUN_TEST(test_enqueue_and_pop_message);
    RUN_TEST(test_message_queue_full);
    RUN_TEST(test_message_queue_fifo_order);
    RUN_TEST(test_enqueue_pending_message_json);
    RUN_TEST(test_pop_from_empty_queue);

    // ACK tracking tests
    RUN_TEST(test_add_and_remove_pending_ack);
    RUN_TEST(test_add_duplicate_ack_updates_timestamp);
    RUN_TEST(test_add_different_msg_type_same_timestamp);
    RUN_TEST(test_max_pending_acks);
    RUN_TEST(test_check_ack_timeouts_no_timeout);
    RUN_TEST(test_check_ack_timeouts_with_timeout);
    RUN_TEST(test_check_ack_timeouts_max_retries);

    // Shared data access tests
    RUN_TEST(test_get_shared_data);
    RUN_TEST(test_get_semaphores);

    // Inventory tests
    RUN_TEST(test_inventory_initialization);
    RUN_TEST(test_modify_inventory_add);
    RUN_TEST(test_modify_inventory_add_accumulates);
    RUN_TEST(test_modify_inventory_reduce);
    RUN_TEST(test_modify_inventory_reduce_partial_depletion);
    RUN_TEST(test_modify_inventory_reduce_zero_quantity);
    RUN_TEST(test_modify_inventory_null_items);
    RUN_TEST(test_get_inventory_count_valid);
    RUN_TEST(test_get_inventory_count_invalid_id);

    return UNITY_END();
}
