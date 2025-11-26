#include "unity.h"
#include "json_manager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void setUp(void) {
}

void tearDown(void) {
}

// Helper to fill common header fields
void fill_header(message_t *msg, const char *type) {
    strncpy(msg->msg_type, type, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg->source_role, "HUB", SOURCE_ROLE_SIZE - 1);
    strncpy(msg->source_id, "HUB001", SOURCE_ID_SIZE - 1);
    strncpy(msg->target_id, "SRV001", TARGET_ID_SIZE - 1);
    strncpy(msg->timestamp, "2025-11-25T10:00:00Z", TIMESTAMP_SIZE - 1);
    strncpy(msg->checksum, "00000000", CHECKSUM_SIZE - 1);
}

void test_serialize_deserialize_auth_request(void) {
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__AUTH_REQUEST);
    
    strncpy(original_msg.payload.client_auth_request.username, "testuser", CREDENTIALS_SIZE - 1);
    strncpy(original_msg.payload.client_auth_request.password, "secret123", CREDENTIALS_SIZE - 1);

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(buffer);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_auth_request.username, deserialized_msg.payload.client_auth_request.username);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_auth_request.password, deserialized_msg.payload.client_auth_request.password);
}

void test_serialize_deserialize_auth_response(void) {
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, SERVER_TO_HUB__AUTH_RESPONSE);
    
    original_msg.payload.server_auth_response.status_code = 200;

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.server_auth_response.status_code, deserialized_msg.payload.server_auth_response.status_code);
}

void test_serialize_deserialize_keepalive(void) {
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__KEEPALIVE);
    
    original_msg.payload.keepalive.message = 'K';

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT8(original_msg.payload.keepalive.message, deserialized_msg.payload.keepalive.message);
}

void test_serialize_deserialize_inventory_update(void) {
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

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    
    // Check Item 1
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[0].item_id, deserialized_msg.payload.inventory_update.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.inventory_update.items[0].item_name, deserialized_msg.payload.inventory_update.items[0].item_name);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[0].quantity, deserialized_msg.payload.inventory_update.items[0].quantity);

    // Check Item 2
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[1].item_id, deserialized_msg.payload.inventory_update.items[1].item_id);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.inventory_update.items[1].item_name, deserialized_msg.payload.inventory_update.items[1].item_name);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.inventory_update.items[1].quantity, deserialized_msg.payload.inventory_update.items[1].quantity);
}

void test_serialize_deserialize_client_emergency(void) {
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__EMERGENCY_ALERT);
    
    original_msg.payload.client_emergency.emergency_code = 911;
    strncpy(original_msg.payload.client_emergency.emergency_type, "FIRE", 19);

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.client_emergency.emergency_code, deserialized_msg.payload.client_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.client_emergency.emergency_type, deserialized_msg.payload.client_emergency.emergency_type);
}

void test_serialize_deserialize_server_emergency(void) {
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT);
    
    original_msg.payload.server_emergency.emergency_code = 999;
    strncpy(original_msg.payload.server_emergency.instructions, "EVACUATE IMMEDIATELY", 99);

    char buffer[MAX_BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.server_emergency.emergency_code, deserialized_msg.payload.server_emergency.emergency_code);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.server_emergency.instructions, deserialized_msg.payload.server_emergency.instructions);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_deserialize_auth_request);
    RUN_TEST(test_serialize_deserialize_auth_response);
    RUN_TEST(test_serialize_deserialize_keepalive);
    RUN_TEST(test_serialize_deserialize_inventory_update);
    RUN_TEST(test_serialize_deserialize_client_emergency);
    RUN_TEST(test_serialize_deserialize_server_emergency);
    return UNITY_END();
}
