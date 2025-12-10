#include "json_manager.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// Helper to fill common header fields
void fill_header(message_t* msg, const char* type)
{
    strncpy(msg->msg_type, type, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg->source_role, "HUB", ROLE_SIZE - 1);
    strncpy(msg->source_id, "HUB001", ID_SIZE - 1);
    strncpy(msg->target_role, "SERVER", ROLE_SIZE - 1);
    strncpy(msg->target_id, "SRV001", ID_SIZE - 1);
    strncpy(msg->timestamp, "2025-11-25T10:00:00Z", TIMESTAMP_SIZE - 1);
    strncpy(msg->checksum, "00000000", CHECKSUM_SIZE - 1);
}

void test_serialize_deserialize_auth_request(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__AUTH_REQUEST);

    strncpy(original_msg.payload.client_auth_request.username, "testuser", CREDENTIALS_SIZE - 1);
    strncpy(original_msg.payload.client_auth_request.password, "secret123", CREDENTIALS_SIZE - 1);

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(buffer);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_auth_request.username,
                             deserialized_msg.payload.client_auth_request.username);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_auth_request.password,
                             deserialized_msg.payload.client_auth_request.password);
}

void test_serialize_deserialize_auth_response(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, SERVER_TO_HUB__AUTH_RESPONSE);

    original_msg.payload.server_auth_response.status_code = 200;

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.server_auth_response.status_code,
                          deserialized_msg.payload.server_auth_response.status_code);
}

void test_serialize_deserialize_keepalive(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__KEEPALIVE);

    original_msg.payload.keepalive.message = 'K';

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT8(original_msg.payload.keepalive.message, deserialized_msg.payload.keepalive.message);
}

void test_serialize_deserialize_inventory_update(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__INVENTORY_UPDATE);

    // Item 1
    original_msg.payload.inventory_update.items[0].item_id = 101;
    strncpy(original_msg.payload.inventory_update.items[0].item_name, "Water", ITEM_NAME_SIZE - 1);
    original_msg.payload.inventory_update.items[0].quantity = 50;

    // Item 2
    original_msg.payload.inventory_update.items[1].item_id = 102;
    strncpy(original_msg.payload.inventory_update.items[1].item_name, "Food", ITEM_NAME_SIZE - 1);
    original_msg.payload.inventory_update.items[1].quantity = 30;

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);

    // Check Item 1
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[0].item_id,
                          deserialized_msg.payload.inventory_update.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.inventory_update.items[0].item_name,
                             deserialized_msg.payload.inventory_update.items[0].item_name);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[0].quantity,
                          deserialized_msg.payload.inventory_update.items[0].quantity);

    // Check Item 2
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[1].item_id,
                          deserialized_msg.payload.inventory_update.items[1].item_id);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.inventory_update.items[1].item_name,
                             deserialized_msg.payload.inventory_update.items[1].item_name);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[1].quantity,
                          deserialized_msg.payload.inventory_update.items[1].quantity);
}

void test_serialize_deserialize_client_emergency(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__EMERGENCY_ALERT);

    original_msg.payload.client_emergency.emergency_code = 911;
    strncpy(original_msg.payload.client_emergency.emergency_type, "FIRE", 19);

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.client_emergency.emergency_code,
                          deserialized_msg.payload.client_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_emergency.emergency_type,
                             deserialized_msg.payload.client_emergency.emergency_type);
}

void test_serialize_deserialize_server_emergency(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT);

    original_msg.payload.server_emergency.emergency_code = 999;
    strncpy(original_msg.payload.server_emergency.instructions, "EVACUATE IMMEDIATELY", 99);

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.server_emergency.emergency_code,
                          deserialized_msg.payload.server_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.server_emergency.instructions,
                             deserialized_msg.payload.server_emergency.instructions);
}

void test_create_auth_request_message(void)
{
    message_t msg;
    int ret = create_auth_request_message(&msg, HUB, "client_0001", "user123", "pass456");
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__AUTH_REQUEST, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", msg.source_id);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_id);
    TEST_ASSERT_EQUAL_STRING("user123", msg.payload.client_auth_request.username);
    TEST_ASSERT_EQUAL_STRING("pass456", msg.payload.client_auth_request.password);
}

void test_create_keepalive_message(void)
{
    message_t msg;
    int ret = create_keepalive_message(&msg, WAREHOUSE, "client_0002", 'K');
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__KEEPALIVE, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0002", msg.source_id);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_id);
    TEST_ASSERT_EQUAL_INT8('K', msg.payload.keepalive.message);
}

void test_create_items_message_stock_request(void)
{
    inventory_item_t items[3] = {{1, "Water", 10}, {2, "Food", 20}, {3, "Medicine", 5}};

    message_t msg;
    int ret = create_items_message(&msg, HUB, "client_0001", SERVER, SERVER, STOCK_REQUEST, items, 3);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__STOCK_REQUEST, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", msg.source_id);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_role);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_id);

    TEST_ASSERT_EQUAL_INT(1, msg.payload.stock_request.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING("Water", msg.payload.stock_request.items[0].item_name);
    TEST_ASSERT_EQUAL_INT(10, msg.payload.stock_request.items[0].quantity);
}

void test_create_items_message_inventory_update(void)
{
    inventory_item_t items[2] = {{101, "Blankets", 15}, {102, "Tents", 8}};

    message_t msg;
    int ret = create_items_message(&msg, WAREHOUSE, "warehouse_001", SERVER, SERVER, INVENTORY_UPDATE, items, 2);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, msg.msg_type);
    TEST_ASSERT_EQUAL_INT(101, msg.payload.inventory_update.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING("Blankets", msg.payload.inventory_update.items[0].item_name);
}

void test_create_auth_response_message(void)
{
    message_t msg;
    int ret = create_auth_response_message(&msg, HUB, "client_hub_001", 200);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_HUB__AUTH_RESPONSE, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_role);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_id);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.target_role);
    TEST_ASSERT_EQUAL_STRING("client_hub_001", msg.target_id);
    TEST_ASSERT_EQUAL_INT(200, msg.payload.server_auth_response.status_code);
}

void test_create_acknowledgment_message(void)
{
    message_t msg;
    const char* test_timestamp = "2025-12-10T10:30:45Z";
    int ret = create_acknowledgment_message(&msg, HUB, "client_0001", SERVER, SERVER, test_timestamp, 200);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", msg.source_id);
    TEST_ASSERT_EQUAL_INT(200, msg.payload.acknowledgment.status_code);
    TEST_ASSERT_EQUAL_STRING(test_timestamp, msg.payload.acknowledgment.ack_for_timestamp);
}

void test_create_client_emergency_message(void)
{
    message_t msg;
    int ret = create_client_emergency_message(&msg, WAREHOUSE, "warehouse_005", 911, "FLOOD");
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__EMERGENCY_ALERT, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("warehouse_005", msg.source_id);
    TEST_ASSERT_EQUAL_INT(911, msg.payload.client_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING("FLOOD", msg.payload.client_emergency.emergency_type);
}

void test_create_server_emergency_message(void)
{
    message_t msg;
    int ret = create_server_emergency_message(&msg, 999, "Evacuate immediately");
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_role);
    TEST_ASSERT_EQUAL_INT(999, msg.payload.server_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING("Evacuate immediately", msg.payload.server_emergency.instructions);
}

void test_checksum_at_end_of_json(void)
{
    message_t msg;
    create_auth_request_message(&msg, HUB, "client_0001", "user", "pass");

    char buffer[BUFFER_SIZE];
    serialize_message_to_json(&msg, buffer);

    // Verify that "checksum" appears after "payload" in the JSON string
    char* payload_pos = strstr(buffer, "\"payload\"");
    char* checksum_pos = strstr(buffer, "\"checksum\"");

    TEST_ASSERT_NOT_NULL(payload_pos);
    TEST_ASSERT_NOT_NULL(checksum_pos);
    TEST_ASSERT_TRUE(checksum_pos > payload_pos);
}

int main(void)
{
    UNITY_BEGIN();
    // Serialization/Deserialization tests
    RUN_TEST(test_serialize_deserialize_auth_request);
    RUN_TEST(test_serialize_deserialize_auth_response);
    RUN_TEST(test_serialize_deserialize_keepalive);
    RUN_TEST(test_serialize_deserialize_inventory_update);
    RUN_TEST(test_serialize_deserialize_client_emergency);
    RUN_TEST(test_serialize_deserialize_server_emergency);

    // Message builder API tests
    RUN_TEST(test_create_auth_request_message);
    RUN_TEST(test_create_keepalive_message);
    RUN_TEST(test_create_items_message_stock_request);
    RUN_TEST(test_create_items_message_inventory_update);
    RUN_TEST(test_create_auth_response_message);
    RUN_TEST(test_create_acknowledgment_message);
    RUN_TEST(test_create_client_emergency_message);
    RUN_TEST(test_create_server_emergency_message);
    RUN_TEST(test_checksum_at_end_of_json);

    return UNITY_END();
}
