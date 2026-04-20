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

    original_msg.payload.server_auth_response.status_code = OK;

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

    strncpy(original_msg.payload.keepalive.message, ALIVE, DESCRIPTION_SIZE - 1);

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.keepalive.message, deserialized_msg.payload.keepalive.message);
}

void test_serialize_deserialize_inventory_update(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__INVENTORY_UPDATE);

    // Item 1
    original_msg.payload.inventory_update.items[0].item_id = 101;
    strncpy(original_msg.payload.inventory_update.items[0].item_name, "WATER", ITEM_NAME_SIZE - 1);
    original_msg.payload.inventory_update.items[0].quantity = 50;

    // Item 2
    original_msg.payload.inventory_update.items[1].item_id = 102;
    strncpy(original_msg.payload.inventory_update.items[1].item_name, "FOOD", ITEM_NAME_SIZE - 1);
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
    strncpy(original_msg.payload.client_emergency.emergency_type, "FIRE", EMERGENCY_TYPE_SIZE - 1);

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
    strncpy(original_msg.payload.server_emergency.instructions, "EVACUATE IMMEDIATELY",
            EMERGENCY_INSTRUCTIONS_SIZE - 1);

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
    int ret = create_keepalive_message(&msg, WAREHOUSE, "client_0002", ALIVE);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__KEEPALIVE, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0002", msg.source_id);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_id);
    TEST_ASSERT_EQUAL_STRING(ALIVE, msg.payload.keepalive.message);
}

void test_create_items_message_stock_request(void)
{
    inventory_item_t items[QUANTITY_ITEMS] = {{1, "FOOD", 10}, {2, "WATER", 20}, {3, "MEDICINE", 5},
                                              {4, "TOOLS", 0}, {5, "GUNS", 0},   {6, "AMMO", 0}};

    message_t msg;
    int ret =
        create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, "client_0001", SERVER, items, QUANTITY_ITEMS, NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__STOCK_REQUEST, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", msg.source_id);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_role);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.target_id);

    TEST_ASSERT_EQUAL_INT(1, msg.payload.stock_request.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING("FOOD", msg.payload.stock_request.items[0].item_name);
    TEST_ASSERT_EQUAL_INT(10, msg.payload.stock_request.items[0].quantity);
}

void test_create_items_message_inventory_update(void)
{
    inventory_item_t items[QUANTITY_ITEMS] = {{1, "FOOD", 15}, {2, "WATER", 8}, {3, "MEDICINE", 0},
                                              {4, "TOOLS", 0}, {5, "GUNS", 0},  {6, "AMMO", 0}};

    message_t msg;
    int ret = create_items_message(&msg, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, "warehouse_001", SERVER, items,
                                   QUANTITY_ITEMS, NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, msg.msg_type);
    TEST_ASSERT_EQUAL_INT(1, msg.payload.inventory_update.items[0].item_id);
    TEST_ASSERT_EQUAL_STRING("FOOD", msg.payload.inventory_update.items[0].item_name);
    TEST_ASSERT_EQUAL_INT(15, msg.payload.inventory_update.items[0].quantity);
}

void test_create_auth_response_message(void)
{
    message_t msg;
    int ret = create_auth_response_message(&msg, HUB, "client_hub_001", OK);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_HUB__AUTH_RESPONSE, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_role);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_id);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.target_role);
    TEST_ASSERT_EQUAL_STRING("client_hub_001", msg.target_id);
    TEST_ASSERT_EQUAL_INT(OK, msg.payload.server_auth_response.status_code);
}

void test_create_acknowledgment_message(void)
{
    message_t msg;
    const char* test_timestamp = "2025-12-10T10:30:45Z";
    int ret = create_acknowledgment_message(&msg, HUB, "client_0001", SERVER, SERVER, test_timestamp, OK);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(HUB, msg.source_role);
    TEST_ASSERT_EQUAL_STRING("client_0001", msg.source_id);
    TEST_ASSERT_EQUAL_INT(OK, msg.payload.acknowledgment.status_code);
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

void test_serialize_deserialize_acknowledgment(void)
{
    message_t original_msg;
    memset(&original_msg, 0, sizeof(message_t));
    fill_header(&original_msg, HUB_TO_SERVER__ACK);

    original_msg.payload.acknowledgment.status_code = OK;
    strncpy(original_msg.payload.acknowledgment.ack_for_timestamp, "2025-12-10T10:30:45Z", TIMESTAMP_SIZE - 1);

    char buffer[BUFFER_SIZE];
    int ret = serialize_message_to_json(&original_msg, buffer);
    TEST_ASSERT_EQUAL_INT(0, ret);

    message_t deserialized_msg;
    ret = deserialize_message_from_json(buffer, &deserialized_msg);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING(original_msg.msg_type, deserialized_msg.msg_type);
    TEST_ASSERT_EQUAL_INT(original_msg.payload.acknowledgment.status_code,
                          deserialized_msg.payload.acknowledgment.status_code);
    TEST_ASSERT_EQUAL_STRING(original_msg.payload.acknowledgment.ack_for_timestamp,
                             deserialized_msg.payload.acknowledgment.ack_for_timestamp);
}

void test_create_items_message_stock_receipt_confirmation(void)
{
    inventory_item_t items[QUANTITY_ITEMS] = {{1, "FOOD", 10}, {2, "WATER", 20}, {3, "MEDICINE", 5},
                                              {4, "TOOLS", 0}, {5, "GUNS", 0},   {6, "AMMO", 0}};

    const char* order_ts = "2025-12-10T15:00:00Z";
    message_t msg;
    int ret = create_items_message(&msg, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, "client_0001", SERVER, items,
                                   QUANTITY_ITEMS, order_ts);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(order_ts, msg.payload.receipt_confirmation.order_timestamp);
}

void test_create_auth_response_message_warehouse(void)
{
    message_t msg;
    int ret = create_auth_response_message(&msg, WAREHOUSE, "warehouse_001", OK);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_WAREHOUSE__AUTH_RESPONSE, msg.msg_type);
    TEST_ASSERT_EQUAL_STRING(SERVER, msg.source_role);
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE, msg.target_role);
    TEST_ASSERT_EQUAL_STRING("warehouse_001", msg.target_id);
    TEST_ASSERT_EQUAL_INT(OK, msg.payload.server_auth_response.status_code);
}

void test_serialize_message_null_inputs(void)
{
    message_t msg;
    char buffer[BUFFER_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, serialize_message_to_json(NULL, buffer));
    TEST_ASSERT_EQUAL_INT(-1, serialize_message_to_json(&msg, NULL));
}

void test_deserialize_message_null_inputs(void)
{
    message_t msg;

    TEST_ASSERT_EQUAL_INT(-1, deserialize_message_from_json(NULL, &msg));
    TEST_ASSERT_EQUAL_INT(-1, deserialize_message_from_json("{}", NULL));
}

void test_deserialize_message_invalid_json(void)
{
    message_t msg;
    int ret = deserialize_message_from_json("not valid json {{{", &msg);
    TEST_ASSERT_EQUAL_INT(-2, ret);
}

void test_create_auth_request_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_auth_request_message(NULL, HUB, "id", "user", "pass"));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_request_message(&msg, NULL, "id", "user", "pass"));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_request_message(&msg, HUB, NULL, "user", "pass"));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_request_message(&msg, HUB, "id", NULL, "pass"));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_request_message(&msg, HUB, "id", "user", NULL));
}

void test_create_keepalive_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_keepalive_message(NULL, HUB, "id", ALIVE));
    TEST_ASSERT_EQUAL_INT(-1, create_keepalive_message(&msg, NULL, "id", ALIVE));
    TEST_ASSERT_EQUAL_INT(-1, create_keepalive_message(&msg, HUB, NULL, ALIVE));
}

void test_create_items_message_null_inputs(void)
{
    message_t msg;
    inventory_item_t items[1] = {{1, "FOOD", 10}};

    TEST_ASSERT_EQUAL_INT(-1, create_items_message(NULL, HUB_TO_SERVER__STOCK_REQUEST, "id", SERVER, items, 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, NULL, "id", SERVER, items, 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, NULL, SERVER, items, 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, "id", NULL, items, 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, "id", SERVER, NULL, 1, NULL));
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, "id", SERVER, items, -1, NULL));
}

void test_create_items_message_unknown_type(void)
{
    message_t msg;
    inventory_item_t items[1] = {{1, "FOOD", 10}};

    // Unknown message type prefix should return -1
    TEST_ASSERT_EQUAL_INT(-1, create_items_message(&msg, "UNKNOWN_TYPE", "id", SERVER, items, 1, NULL));
}

void test_create_auth_response_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_auth_response_message(NULL, HUB, "id", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_response_message(&msg, NULL, "id", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_auth_response_message(&msg, HUB, NULL, OK));
}

void test_create_auth_response_invalid_role(void)
{
    message_t msg;
    // Invalid target_role should return -1
    TEST_ASSERT_EQUAL_INT(-1, create_auth_response_message(&msg, "INVALID_ROLE", "id", OK));
}

void test_create_acknowledgment_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(NULL, HUB, "id", SERVER, SERVER, "ts", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, NULL, "id", SERVER, SERVER, "ts", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, HUB, NULL, SERVER, SERVER, "ts", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, HUB, "id", NULL, SERVER, "ts", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, HUB, "id", SERVER, NULL, "ts", OK));
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, HUB, "id", SERVER, SERVER, NULL, OK));
}

void test_create_acknowledgment_invalid_roles(void)
{
    message_t msg;
    // Invalid role combination should return -1
    TEST_ASSERT_EQUAL_INT(-1, create_acknowledgment_message(&msg, "INVALID", "id", "INVALID", SERVER, "ts", OK));
}

void test_create_client_emergency_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_client_emergency_message(NULL, HUB, "id", 911, "FIRE"));
    TEST_ASSERT_EQUAL_INT(-1, create_client_emergency_message(&msg, NULL, "id", 911, "FIRE"));
    TEST_ASSERT_EQUAL_INT(-1, create_client_emergency_message(&msg, HUB, NULL, 911, "FIRE"));
    TEST_ASSERT_EQUAL_INT(-1, create_client_emergency_message(&msg, HUB, "id", 911, NULL));
}

void test_create_server_emergency_null_inputs(void)
{
    message_t msg;
    TEST_ASSERT_EQUAL_INT(-1, create_server_emergency_message(NULL, 999, "Evacuate"));
    TEST_ASSERT_EQUAL_INT(-1, create_server_emergency_message(&msg, 999, NULL));
}

void test_create_acknowledgment_all_valid_combinations(void)
{
    message_t msg;

    // HUB -> SERVER
    TEST_ASSERT_EQUAL_INT(0, create_acknowledgment_message(&msg, HUB, "hub1", SERVER, "srv", "ts", OK));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, msg.msg_type);

    // WAREHOUSE -> SERVER
    TEST_ASSERT_EQUAL_INT(0, create_acknowledgment_message(&msg, WAREHOUSE, "wh1", SERVER, "srv", "ts", OK));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__ACK, msg.msg_type);

    // SERVER -> HUB
    TEST_ASSERT_EQUAL_INT(0, create_acknowledgment_message(&msg, SERVER, "srv", HUB, "hub1", "ts", OK));
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_HUB__ACK, msg.msg_type);

    // SERVER -> WAREHOUSE
    TEST_ASSERT_EQUAL_INT(0, create_acknowledgment_message(&msg, SERVER, "srv", WAREHOUSE, "wh1", "ts", OK));
    TEST_ASSERT_EQUAL_STRING(SERVER_TO_WAREHOUSE__ACK, msg.msg_type);
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
    RUN_TEST(test_serialize_deserialize_acknowledgment);

    // Message builder API tests
    RUN_TEST(test_create_auth_request_message);
    RUN_TEST(test_create_keepalive_message);
    RUN_TEST(test_create_items_message_stock_request);
    RUN_TEST(test_create_items_message_inventory_update);
    RUN_TEST(test_create_items_message_stock_receipt_confirmation);
    RUN_TEST(test_create_auth_response_message);
    RUN_TEST(test_create_auth_response_message_warehouse);
    RUN_TEST(test_create_acknowledgment_message);
    RUN_TEST(test_create_acknowledgment_all_valid_combinations);
    RUN_TEST(test_create_client_emergency_message);
    RUN_TEST(test_create_server_emergency_message);
    RUN_TEST(test_checksum_at_end_of_json);

    // Error handling / NULL input tests
    RUN_TEST(test_serialize_message_null_inputs);
    RUN_TEST(test_deserialize_message_null_inputs);
    RUN_TEST(test_deserialize_message_invalid_json);
    RUN_TEST(test_create_auth_request_null_inputs);
    RUN_TEST(test_create_keepalive_null_inputs);
    RUN_TEST(test_create_items_message_null_inputs);
    RUN_TEST(test_create_items_message_unknown_type);
    RUN_TEST(test_create_auth_response_null_inputs);
    RUN_TEST(test_create_auth_response_invalid_role);
    RUN_TEST(test_create_acknowledgment_null_inputs);
    RUN_TEST(test_create_acknowledgment_invalid_roles);
    RUN_TEST(test_create_client_emergency_null_inputs);
    RUN_TEST(test_create_server_emergency_null_inputs);

    return UNITY_END();
}
