#include "ipc.h"
#include "json_manager.h"
#include "unity.h"
#include <string.h>
#include <time.h>
#include <unistd.h>

void setUp(void)
{
    // Initialize IPC for each test
    ipc_init();
}

void tearDown(void)
{
    // Cleanup IPC after each test
    ipc_cleanup();
}

// ==================== MESSAGE QUEUE TESTS ====================

void test_enqueue_and_pop_message(void)
{
    message_t msg_in, msg_out;

    // Create a test message
    strncpy(msg_in.msg_type, "WAREHOUSE_TO_SERVER__STATUS", sizeof(msg_in.msg_type) - 1);
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

// ==================== ACK TRACKING TESTS ====================

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
    const char* msg_type = "WAREHOUSE_TO_SERVER__STATUS";
    const char* json_msg = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__STATUS\"}";

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
    const char* msg_type_1 = "WAREHOUSE_TO_SERVER__STATUS";
    const char* msg_type_2 = "WAREHOUSE_TO_SERVER__INVENTORY_UPDATE";
    const char* json_msg_1 = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__STATUS\"}";
    const char* json_msg_2 = "{\"msg_type\":\"WAREHOUSE_TO_SERVER__INVENTORY_UPDATE\"}";

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

// ==================== SHARED DATA ACCESS TESTS ====================

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

// ==================== MAIN ====================

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

    return UNITY_END();
}
