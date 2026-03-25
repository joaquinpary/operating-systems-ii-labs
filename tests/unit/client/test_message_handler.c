#include "json_manager.h"
#include "logger.h"
#include "message_handler.h"
#include "shared_state.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_LOG_FILE_SIZE (10 * 1024 * 1024)
#define TEST_LOG_BACKUPS 3

void setUp(void)
{
    logger_config_t config = {.log_file_path = "/tmp/test_message_handler.log",
                              .max_file_size = TEST_LOG_FILE_SIZE,
                              .max_backup_files = TEST_LOG_BACKUPS,
                              .min_level = LOG_DEBUG};
    log_init(&config);

    if (ipc_init("test_msg_handler") != 0)
    {
        TEST_FAIL_MESSAGE("Failed to initialize IPC in setUp");
    }

    shared_data_t* shared_data = get_shared_data();
    memset(shared_data, 0, sizeof(shared_data_t));
    strncpy(shared_data->client_role, HUB, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "HUB001", sizeof(shared_data->client_id) - 1);

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

void test_handle_null_message(void)
{
    int result = handle_server_message(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_handle_ack_removes_pending(void)
{
    add_pending_ack("2025-11-25T09:59:00.000Z", HUB_TO_SERVER__INVENTORY_UPDATE, "{\"test\":\"data\"}");

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__ACK, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.payload.acknowledgment.ack_for_timestamp, "2025-11-25T09:59:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.acknowledgment.status_code = 200;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    shared_data_t* shared_data = get_shared_data();
    int found = 0;
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active &&
            strcmp(shared_data->pending_acks[i].msg_id, "2025-11-25T09:59:00.000Z") == 0)
        {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, found);
}

void test_handle_ack_warehouse(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);

    add_pending_ack("2025-11-25T10:00:00.000Z", WAREHOUSE_TO_SERVER__REPLENISH_REQUEST, "{}");

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__ACK, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.payload.acknowledgment.ack_for_timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_handle_auth_response_hub(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__AUTH_RESPONSE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_auth_response.status_code = 200;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, ack_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-11-25T10:00:00.000Z", ack_msg.payload.acknowledgment.ack_for_timestamp);
    TEST_ASSERT_EQUAL_INT(200, ack_msg.payload.acknowledgment.status_code);
}

void test_handle_auth_response_warehouse(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "WH001", sizeof(shared_data->client_id) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T11:00:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_auth_response.status_code = 401;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify ACK was enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__ACK, ack_msg.msg_type);
}

void test_handle_inventory_update_hub(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__INVENTORY_UPDATE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T12:00:00.000Z", TIMESTAMP_SIZE - 1);

    // Add 20 units to first item
    msg.payload.inventory_update.items[0].item_id = 1;
    strncpy(msg.payload.inventory_update.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.inventory_update.items[0].quantity = 20;

    shared_data_t* shared_data = get_shared_data();
    int initial_qty = shared_data->inventory_item[0].quantity;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify inventory increased
    TEST_ASSERT_EQUAL_INT(initial_qty + 20, shared_data->inventory_item[0].quantity);

    // Verify ACK was enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, ack_msg.msg_type);
}

void test_handle_inventory_update_warehouse(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__INVENTORY_UPDATE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T13:00:00.000Z", TIMESTAMP_SIZE - 1);

    // Update multiple items
    for (int i = 0; i < 3; i++)
    {
        msg.payload.inventory_update.items[i].item_id = i + 1;
        snprintf(msg.payload.inventory_update.items[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        msg.payload.inventory_update.items[i].quantity = 5;
    }

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify all items increased
    TEST_ASSERT_EQUAL_INT(55, shared_data->inventory_item[0].quantity);
    TEST_ASSERT_EQUAL_INT(55, shared_data->inventory_item[1].quantity);
    TEST_ASSERT_EQUAL_INT(55, shared_data->inventory_item[2].quantity);
}

void test_handle_dispatch_order_single_item(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "WH001", sizeof(shared_data->client_id) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.target_id, "HUB002", ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T14:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.order_stock.items[0].item_id = 1;
    strncpy(msg.payload.order_stock.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.order_stock.items[0].quantity = 10;

    int initial_qty = shared_data->inventory_item[0].quantity;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify inventory reduced
    TEST_ASSERT_EQUAL_INT(initial_qty - 10, shared_data->inventory_item[0].quantity);

    // Verify ACK enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__ACK, ack_msg.msg_type);

    // Verify shipment notice enqueued
    message_t shipment_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&shipment_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, shipment_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("HUB002", shipment_msg.target_id);
    TEST_ASSERT_EQUAL_INT(10, shipment_msg.payload.shipment_notice.items[0].quantity);
}

void test_handle_dispatch_order_multiple_items(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "WH001", sizeof(shared_data->client_id) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.target_id, "HUB003", ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T15:00:00.000Z", TIMESTAMP_SIZE - 1);

    // Dispatch 3 different items
    for (int i = 0; i < 3; i++)
    {
        msg.payload.order_stock.items[i].item_id = i + 1;
        snprintf(msg.payload.order_stock.items[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        msg.payload.order_stock.items[i].quantity = 5 + i;
    }

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify inventory reduced for all items
    TEST_ASSERT_EQUAL_INT(45, shared_data->inventory_item[0].quantity); // 50 - 5
    TEST_ASSERT_EQUAL_INT(44, shared_data->inventory_item[1].quantity); // 50 - 6
    TEST_ASSERT_EQUAL_INT(43, shared_data->inventory_item[2].quantity); // 50 - 7

    // Verify ACK and shipment notice
    message_t ack_msg, shipment_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&shipment_msg));
    TEST_ASSERT_EQUAL_STRING("HUB003", shipment_msg.target_id);
}

void test_handle_restock_notice_warehouse(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "WH001", sizeof(shared_data->client_id) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T16:00:00.000Z", TIMESTAMP_SIZE - 1);
    strncpy(msg.payload.restock_notice.order_timestamp, "2025-11-25T16:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.restock_notice.items[0].item_id = 1;
    strncpy(msg.payload.restock_notice.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.restock_notice.items[0].quantity = 30;

    int initial_qty = shared_data->inventory_item[0].quantity;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify inventory increased
    TEST_ASSERT_EQUAL_INT(initial_qty + 30, shared_data->inventory_item[0].quantity);

    // Verify ACK enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__ACK, ack_msg.msg_type);

    // Verify receipt confirmation enqueued
    message_t receipt_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&receipt_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, receipt_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-11-25T16:00:00.000Z", receipt_msg.payload.receipt_confirmation.order_timestamp);
}

void test_handle_incoming_stock_notice_hub(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__INCOMING_STOCK_NOTICE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T17:00:00.000Z", TIMESTAMP_SIZE - 1);
    strncpy(msg.payload.restock_notice.order_timestamp, "2025-11-25T17:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.restock_notice.items[1].item_id = 2;
    strncpy(msg.payload.restock_notice.items[1].item_name, "ITEM_2", ITEM_NAME_SIZE - 1);
    msg.payload.restock_notice.items[1].quantity = 25;

    shared_data_t* shared_data = get_shared_data();
    int initial_qty = shared_data->inventory_item[1].quantity;

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify inventory increased
    TEST_ASSERT_EQUAL_INT(initial_qty + 25, shared_data->inventory_item[1].quantity);

    // Verify ACK enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, ack_msg.msg_type);

    // Verify receipt confirmation enqueued (HUB version)
    message_t receipt_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&receipt_msg));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, receipt_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-11-25T17:00:00.000Z", receipt_msg.payload.receipt_confirmation.order_timestamp);
}

void test_handle_incoming_stock_all_items(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__INCOMING_STOCK_NOTICE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T18:00:00.000Z", TIMESTAMP_SIZE - 1);

    // Restock all 6 items
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        msg.payload.restock_notice.items[i].item_id = i + 1;
        snprintf(msg.payload.restock_notice.items[i].item_name, ITEM_NAME_SIZE, "ITEM_%d", i + 1);
        msg.payload.restock_notice.items[i].quantity = 10;
    }

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify all items increased
    shared_data_t* shared_data = get_shared_data();
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        TEST_ASSERT_EQUAL_INT(60, shared_data->inventory_item[i].quantity);
    }
}

void test_handle_unknown_message_type(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, "UNKNOWN_MESSAGE_TYPE", MESSAGE_TYPE_SIZE - 1);

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result); // Should return 0 with warning
}

void test_handle_auth_response_when_queue_full(void)
{
    shared_data_t* shared_data = get_shared_data();

    // Fill the message queue (max 10 messages)
    shared_data->message_count = 10;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__AUTH_RESPONSE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.source_role, SERVER, ROLE_SIZE - 1);
    strncpy(msg.source_id, SERVER, ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_auth_response.status_code = 200;

    // Should fail because queue is full
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    // Restore queue state
    shared_data->message_count = 0;
}

void test_handle_inventory_update_when_queue_full(void)
{
    shared_data_t* shared_data = get_shared_data();

    // Fill the message queue
    shared_data->message_count = 10;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__INVENTORY_UPDATE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.inventory_update.items[0].item_id = 1;
    strncpy(msg.payload.inventory_update.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.inventory_update.items[0].quantity = 20;

    int initial_qty = shared_data->inventory_item[0].quantity;

    // Should fail because cannot enqueue ACK
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    // Note: inventory was modified before enqueue failure
    // This is expected behavior - inventory update happens first
    TEST_ASSERT_EQUAL_INT(initial_qty + 20, shared_data->inventory_item[0].quantity);

    // Restore states
    shared_data->message_count = 0;
    shared_data->inventory_item[0].quantity = initial_qty;
}

void test_handle_dispatch_order_when_queue_full_on_ack(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);

    // Fill queue to fail on first enqueue (ACK)
    shared_data->message_count = 10;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.target_id, "HUB001", ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.order_stock.items[0].item_id = 1;
    strncpy(msg.payload.order_stock.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.order_stock.items[0].quantity = 10;

    // Should fail when trying to enqueue ACK
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    shared_data->message_count = 0;
}

void test_handle_dispatch_order_when_queue_full_on_shipment(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);

    // Set inventory
    shared_data->inventory_item[0].item_id = 1;
    strncpy(shared_data->inventory_item[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    shared_data->inventory_item[0].quantity = 100;

    // Fill queue almost full (9 messages) - ACK will succeed, shipment will fail
    shared_data->message_count = 9;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.target_id, "HUB001", ID_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.order_stock.items[0].item_id = 1;
    strncpy(msg.payload.order_stock.items[0].item_name, "ITEM_1", ITEM_NAME_SIZE - 1);
    msg.payload.order_stock.items[0].quantity = 10;

    int initial_qty = shared_data->inventory_item[0].quantity;

    // Should fail when trying to enqueue shipment notice (after ACK succeeds)
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    // Verify ACK was enqueued (queue now has 10 items)
    TEST_ASSERT_EQUAL_INT(10, shared_data->message_count);

    // Verify inventory was reduced
    TEST_ASSERT_EQUAL_INT(initial_qty - 10, shared_data->inventory_item[0].quantity);

    // Restore
    shared_data->message_count = 0;
    shared_data->inventory_item[0].quantity = initial_qty;
}

void test_handle_restock_notice_when_queue_full_on_receipt(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, HUB, sizeof(shared_data->client_role) - 1);

    // Fill queue almost full - ACK succeeds, receipt confirmation fails
    shared_data->message_count = 9;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_HUB__INCOMING_STOCK_NOTICE, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T10:00:00.000Z", TIMESTAMP_SIZE - 1);

    msg.payload.restock_notice.items[1].item_id = 2;
    strncpy(msg.payload.restock_notice.items[1].item_name, "ITEM_2", ITEM_NAME_SIZE - 1);
    msg.payload.restock_notice.items[1].quantity = 30;

    int initial_qty = shared_data->inventory_item[1].quantity;

    // Should fail on receipt confirmation enqueue
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    // Verify ACK was enqueued and inventory updated
    TEST_ASSERT_EQUAL_INT(10, shared_data->message_count);
    TEST_ASSERT_EQUAL_INT(initial_qty + 30, shared_data->inventory_item[1].quantity);

    // Restore
    shared_data->message_count = 0;
    shared_data->inventory_item[1].quantity = initial_qty;
}

void test_handle_server_emergency_alert(void)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T20:00:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_emergency.emergency_code = 2001;
    strncpy(msg.payload.server_emergency.instructions, "Evacuate immediately", EMERGENCY_INSTRUCTIONS_SIZE - 1);

    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify ACK was enqueued
    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(HUB_TO_SERVER__ACK, ack_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-11-25T20:00:00.000Z", ack_msg.payload.acknowledgment.ack_for_timestamp);
    TEST_ASSERT_EQUAL_INT(200, ack_msg.payload.acknowledgment.status_code);
}

void test_handle_server_emergency_alert_when_queue_full(void)
{
    shared_data_t* shared_data = get_shared_data();
    shared_data->message_count = 10;

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T20:01:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_emergency.emergency_code = 2003;
    strncpy(msg.payload.server_emergency.instructions, "Lockdown", EMERGENCY_INSTRUCTIONS_SIZE - 1);

    // Should fail because queue is full and ACK cannot be enqueued
    int result = handle_server_message(&msg);
    TEST_ASSERT_EQUAL_INT(-1, result);

    shared_data->message_count = 0;
}

void test_handle_server_emergency_alert_warehouse(void)
{
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, WAREHOUSE, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, "WH001", sizeof(shared_data->client_id) - 1);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strncpy(msg.msg_type, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT, MESSAGE_TYPE_SIZE - 1);
    strncpy(msg.timestamp, "2025-11-25T20:02:00.000Z", TIMESTAMP_SIZE - 1);
    msg.payload.server_emergency.emergency_code = 2004;
    strncpy(msg.payload.server_emergency.instructions, "Shelter in place", EMERGENCY_INSTRUCTIONS_SIZE - 1);

    TEST_ASSERT_EQUAL_INT(0, handle_server_message(&msg));

    message_t ack_msg;
    TEST_ASSERT_EQUAL_INT(0, pop_pending_message(&ack_msg));
    TEST_ASSERT_EQUAL_STRING(WAREHOUSE_TO_SERVER__ACK, ack_msg.msg_type);
    TEST_ASSERT_EQUAL_STRING("2025-11-25T20:02:00.000Z", ack_msg.payload.acknowledgment.ack_for_timestamp);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_handle_null_message);

    RUN_TEST(test_handle_ack_removes_pending);
    RUN_TEST(test_handle_ack_warehouse);

    RUN_TEST(test_handle_auth_response_hub);
    RUN_TEST(test_handle_auth_response_warehouse);

    RUN_TEST(test_handle_inventory_update_hub);
    RUN_TEST(test_handle_inventory_update_warehouse);

    RUN_TEST(test_handle_dispatch_order_single_item);
    RUN_TEST(test_handle_dispatch_order_multiple_items);

    RUN_TEST(test_handle_restock_notice_warehouse);
    RUN_TEST(test_handle_incoming_stock_notice_hub);
    RUN_TEST(test_handle_incoming_stock_all_items);

    RUN_TEST(test_handle_unknown_message_type);

    RUN_TEST(test_handle_auth_response_when_queue_full);
    RUN_TEST(test_handle_inventory_update_when_queue_full);
    RUN_TEST(test_handle_dispatch_order_when_queue_full_on_ack);
    RUN_TEST(test_handle_dispatch_order_when_queue_full_on_shipment);
    RUN_TEST(test_handle_restock_notice_when_queue_full_on_receipt);

    RUN_TEST(test_handle_server_emergency_alert);
    RUN_TEST(test_handle_server_emergency_alert_when_queue_full);
    RUN_TEST(test_handle_server_emergency_alert_warehouse);

    return UNITY_END();
}
