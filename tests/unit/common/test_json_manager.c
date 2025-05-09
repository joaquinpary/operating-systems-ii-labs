#include "json_manager.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

init_params_client params_warehouse = {
    .client_id = "warehouse0001",
    .username = "user_test",
    .password = "pass_test",
    .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};

init_params_client params_hub = {
    .client_id = "hub0001",
    .username = "user_test",
    .password = "pass_test",
    .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};

inventory_item items[] = {
    {.item = "water", .quantity = 100}, {.item = "food", .quantity = 100}, {.item = "medicine", .quantity = 100},
    {.item = "guns", .quantity = 100},  {.item = "ammo", .quantity = 100}, {.item = "tools", .quantity = 100},
};

void setUp(void)
{
}

void tearDown(void)
{
}

void test_get_timestamp(void)
{
    char* timestamp = get_timestamp();
    TEST_ASSERT_NOT_NULL(timestamp);
    free(timestamp);
}

void test_validate_checksum_success(void)
{
    const char* json_string =
        "{\"type\":\"server_w_stock_warehouse\",\"payload\":{\"items\":[{\"item\":\"water\",\"quantity\":100},{"
        "\"item\":\"food\",\"quantity\":100},{\"item\":\"medicine\",\"quantity\":100},{\"item\":\"guns\",\"quantity\":"
        "100},{\"item\":\"ammo\",\"quantity\":100},{\"item\":\"tools\",\"quantity\":100}],\"timestamp\":\"2025-04-"
        "17T03:17:14Z\"},\"checksum\":\"9A6400D0\"}";
    int result = validate_checksum(json_string);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_validate_checksum_failure(void)
{
    const char* json_string =
        "{\"type\":\"server_w_stock_warehouse\",\"payload\":{\"items\":[{\"item\":\"water\",\"quantity\":100},{"
        "\"item\":\"food\",\"quantity\":100},{\"item\":\"medicine\",\"quantity\":100},{\"item\":\"guns\",\"quantity\":"
        "100},{\"item\":\"ammo\",\"quantity\":100},{\"item\":\"tools\",\"quantity\":100}],\"timestamp\":\"2025-04-"
        "17T03:17:14Z\"},\"checksum\":\"9A6400D1\"}";
    int result = validate_checksum(json_string);
    TEST_ASSERT_EQUAL_INT(1, result);
}

void test_client_auth_request(void)
{
    client_auth_request request = create_client_auth_request(params_warehouse.client_id, params_warehouse.client_type,
                                                             params_warehouse.username, params_warehouse.password);
    char* string = serialize_client_auth_request(&request);
    TEST_ASSERT_NOT_NULL(string);
    client_auth_request deserialized_request = deserialize_client_auth_request(string);
    TEST_ASSERT_EQUAL_STRING(request.type, deserialized_request.type);
    TEST_ASSERT_EQUAL_STRING(request.payload.client_id, deserialized_request.payload.client_id);
    TEST_ASSERT_EQUAL_STRING(request.payload.type, deserialized_request.payload.type);
    TEST_ASSERT_EQUAL_STRING(request.payload.username, deserialized_request.payload.username);
    TEST_ASSERT_EQUAL_STRING(request.payload.password, deserialized_request.payload.password);
    TEST_ASSERT_EQUAL_STRING(request.payload.timestamp, deserialized_request.payload.timestamp);
    free(string);
}

void test_server_auth_response(void)
{
    server_auth_response response = create_server_auth_response("success", "session_token", "Authenticated");
    char* string = serialize_server_auth_response(&response);
    TEST_ASSERT_NOT_NULL(string);
    server_auth_response deserialized_response = deserialize_server_auth_response(string);
    TEST_ASSERT_EQUAL_STRING(response.type, deserialized_response.type);
    TEST_ASSERT_EQUAL_STRING(response.payload.status, deserialized_response.payload.status);
    TEST_ASSERT_EQUAL_STRING(response.payload.session_token, deserialized_response.payload.session_token);
    TEST_ASSERT_EQUAL_STRING(response.payload.message, deserialized_response.payload.message);
    TEST_ASSERT_EQUAL_STRING(response.payload.timestamp, deserialized_response.payload.timestamp);
    free(string);
}

void test_client_keepalive(void)
{
    client_keepalive keepalive = create_client_keepalive(params_warehouse.username, "session_token");
    char* string = serialize_client_keepalive(&keepalive);
    TEST_ASSERT_NOT_NULL(string);
    client_keepalive deserialized_keepalive = deserialize_client_keepalive(string);
    TEST_ASSERT_EQUAL_STRING(keepalive.type, deserialized_keepalive.type);
    TEST_ASSERT_EQUAL_STRING(keepalive.payload.username, deserialized_keepalive.payload.username);
    TEST_ASSERT_EQUAL_STRING(keepalive.payload.session_token, deserialized_keepalive.payload.session_token);
    TEST_ASSERT_EQUAL_STRING(keepalive.payload.timestamp, deserialized_keepalive.payload.timestamp);
    free(string);
}

void test_client_inventory_update(void)
{
    client_inventory_update update =
        create_client_inventory_update(params_warehouse.username, "session_token", items, ITEM_TYPE);
    char* string = serialize_client_inventory_update(&update, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    client_inventory_update deserialized_update = deserialize_client_inventory_update(string);
    TEST_ASSERT_EQUAL_STRING(update.type, deserialized_update.type);
    TEST_ASSERT_EQUAL_STRING(update.payload.username, deserialized_update.payload.username);
    TEST_ASSERT_EQUAL_STRING(update.payload.session_token, deserialized_update.payload.session_token);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(update.payload.items[i].item, deserialized_update.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(update.payload.items[i].quantity, deserialized_update.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(update.payload.timestamp, deserialized_update.payload.timestamp);
    free(string);
}

void test_server_emergency_alert(void)
{
    server_emergency_alert alert = create_server_emergency_alert("enemy_thread");
    char* string = serialize_server_emergency_alert(&alert);
    TEST_ASSERT_NOT_NULL(string);
    server_emergency_alert deserialized_alert = deserialize_server_emergency_alert(string);
    TEST_ASSERT_EQUAL_STRING(alert.type, deserialized_alert.type);
    TEST_ASSERT_EQUAL_STRING(alert.payload.alert_type, deserialized_alert.payload.alert_type);
    TEST_ASSERT_EQUAL_STRING(alert.payload.timestamp, deserialized_alert.payload.timestamp);
    free(string);
}

void test_client_acknowledgment(void)
{
    client_acknowledgment acknowledgment =
        create_client_acknowledgment(params_warehouse.username, "session_token", "success");
    char* string = serialize_client_acknowledgment(&acknowledgment);
    TEST_ASSERT_NOT_NULL(string);
    client_acknowledgment deserialized_acknowledgment = deserialize_client_acknowledgment(string);
    TEST_ASSERT_EQUAL_STRING(acknowledgment.type, deserialized_acknowledgment.type);
    TEST_ASSERT_EQUAL_STRING(acknowledgment.payload.username, deserialized_acknowledgment.payload.username);
    TEST_ASSERT_EQUAL_STRING(acknowledgment.payload.session_token, deserialized_acknowledgment.payload.session_token);
    TEST_ASSERT_EQUAL_STRING(acknowledgment.payload.status, deserialized_acknowledgment.payload.status);
    TEST_ASSERT_EQUAL_STRING(acknowledgment.payload.timestamp, deserialized_acknowledgment.payload.timestamp);
    free(string);
}

void test_client_infection_alert(void)
{
    client_emergency_alert alert = create_client_infection_alert(params_warehouse.username, "session_token");
    char* string = serialize_client_infection_alert(&alert);
    TEST_ASSERT_NOT_NULL(string);
    client_emergency_alert deserialized_alert = deserialize_client_infection_alert(string);
    TEST_ASSERT_EQUAL_STRING(alert.type, deserialized_alert.type);
    TEST_ASSERT_EQUAL_STRING(alert.payload.username, deserialized_alert.payload.username);
    TEST_ASSERT_EQUAL_STRING(alert.payload.session_token, deserialized_alert.payload.session_token);
    TEST_ASSERT_EQUAL_STRING(alert.payload.timestamp, deserialized_alert.payload.timestamp);
    free(string);
}

void test_server_w_stock_hub(void)
{
    server_w_stock_hub stock = create_server_w_stock_hub(items, ITEM_TYPE);
    char* string = serialize_server_w_stock_hub(&stock, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    server_w_stock_hub deserialized_stock = deserialize_server_w_stock_hub(string);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(stock.payload.items[i].item, deserialized_stock.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(stock.payload.items[i].quantity, deserialized_stock.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(stock.payload.timestamp, deserialized_stock.payload.timestamp);
    free(string);
}

void test_warehouse_send_stock_to_hub(void)
{
    warehouse_send_stock_to_hub stock =
        create_warehouse_send_stock_to_hub(params_warehouse.username, "session_token", items, ITEM_TYPE);
    char* string = serialize_warehouse_send_stock_to_hub(&stock, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    warehouse_send_stock_to_hub deserialized_stock = deserialize_warehouse_send_stock_to_hub(string);
    TEST_ASSERT_EQUAL_STRING(stock.type, deserialized_stock.type);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(stock.payload.items[i].item, deserialized_stock.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(stock.payload.items[i].quantity, deserialized_stock.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(stock.payload.timestamp, deserialized_stock.payload.timestamp);
    free(string);
}

void test_warehouse_request_stock(void)
{
    warehouse_request_stock request =
        create_warehouse_request_stock(params_warehouse.username, "session_token", items, ITEM_TYPE);
    char* string = serialize_warehouse_request_stock(&request, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    warehouse_request_stock deserialized_request = deserialize_warehouse_request_stock(string);
    TEST_ASSERT_EQUAL_STRING(request.type, deserialized_request.type);
    TEST_ASSERT_EQUAL_STRING(request.payload.username, deserialized_request.payload.username);
    TEST_ASSERT_EQUAL_STRING(request.payload.session_token, deserialized_request.payload.session_token);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(request.payload.items[i].item, deserialized_request.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(request.payload.items[i].quantity, deserialized_request.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(request.payload.timestamp, deserialized_request.payload.timestamp);
    free(string);
}

void test_server_w_stock_warehouse(void)
{
    server_w_stock_warehouse stock = create_server_w_stock_warehouse(items, ITEM_TYPE);
    char* string = serialize_server_w_stock_warehouse(&stock, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    server_w_stock_warehouse deserialized_stock = deserialize_server_w_stock_warehouse(string);
    TEST_ASSERT_EQUAL_STRING(stock.type, deserialized_stock.type);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(stock.payload.items[i].item, deserialized_stock.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(stock.payload.items[i].quantity, deserialized_stock.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(stock.payload.timestamp, deserialized_stock.payload.timestamp);
    free(string);
}

void test_hub_request_stock(void)
{
    hub_request_stock request = create_hub_request_stock(params_hub.username, "session_token", items, ITEM_TYPE);
    char* string = serialize_hub_request_stock(&request, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    hub_request_stock deserialized_request = deserialize_hub_request_stock(string);
    TEST_ASSERT_EQUAL_STRING(request.type, deserialized_request.type);
    TEST_ASSERT_EQUAL_STRING(request.payload.username, deserialized_request.payload.username);
    TEST_ASSERT_EQUAL_STRING(request.payload.session_token, deserialized_request.payload.session_token);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(request.payload.items[i].item, deserialized_request.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(request.payload.items[i].quantity, deserialized_request.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(request.payload.timestamp, deserialized_request.payload.timestamp);
    free(string);
}

void test_server_h_send_stock(void)
{
    server_h_send_stock stock = create_server_h_send_stock(items, ITEM_TYPE);
    char* string = serialize_server_h_send_stock(&stock, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    server_h_send_stock deserialized_stock = deserialize_server_h_send_stock(string);
    TEST_ASSERT_EQUAL_STRING(stock.type, deserialized_stock.type);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(stock.payload.items[i].item, deserialized_stock.payload.items[i].item);
        TEST_ASSERT_EQUAL_INT(stock.payload.items[i].quantity, deserialized_stock.payload.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(stock.payload.timestamp, deserialized_stock.payload.timestamp);
    free(string);
}

void test_cli_message(void)
{
    cli_message message = create_cli_message(params_warehouse.username, "session_token", 1);
    char* string = serialize_cli_message(&message);
    TEST_ASSERT_NOT_NULL(string);
    cli_message deserialized_message = deserialize_cli_message(string);
    TEST_ASSERT_EQUAL_STRING(message.type, deserialized_message.type);
    TEST_ASSERT_EQUAL_STRING(message.username, deserialized_message.username);
    TEST_ASSERT_EQUAL_STRING(message.session_token, deserialized_message.session_token);
    TEST_ASSERT_EQUAL_STRING(message.message_type, deserialized_message.message_type);
    TEST_ASSERT_EQUAL_STRING(message.timestamp, deserialized_message.timestamp);
    free(string);
}

void test_end_of_message(void)
{
    end_of_message message = create_end_of_message();
    char* string = serialize_end_of_message(&message);
    TEST_ASSERT_NOT_NULL(string);
    end_of_message deserialized_message = deserialize_end_of_message(string);
    TEST_ASSERT_EQUAL_STRING(message.type, deserialized_message.type);
    TEST_ASSERT_EQUAL_STRING(message.timestamp, deserialized_message.timestamp);
    free(string);
}

void test_server_transaction_history(void)
{
    server_transaction_history history = create_server_transaction_history(
        1, 1, 1, "2025-04-17T03:17:14Z", "2025-04-17T03:17:14Z", "2025-04-17T03:17:14Z", items, "warehouse", "hub");
    char* string = serialize_server_transaction_history(&history, ITEM_TYPE);
    TEST_ASSERT_NOT_NULL(string);
    server_transaction_history deserialized_history = deserialize_server_transaction_history(string);
    TEST_ASSERT_EQUAL_STRING(history.type, deserialized_history.type);
    TEST_ASSERT_EQUAL_INT(history.id, deserialized_history.id);
    TEST_ASSERT_EQUAL_INT(history.hub_id, deserialized_history.hub_id);
    TEST_ASSERT_EQUAL_INT(history.warehouse_id, deserialized_history.warehouse_id);
    TEST_ASSERT_EQUAL_STRING(history.timestamp_requested, deserialized_history.timestamp_requested);
    TEST_ASSERT_EQUAL_STRING(history.timestamp_dispatched, deserialized_history.timestamp_dispatched);
    TEST_ASSERT_EQUAL_STRING(history.timestamp_received, deserialized_history.timestamp_received);
    for (int i = 0; i < ITEM_TYPE; i++)
    {
        TEST_ASSERT_EQUAL_STRING(history.items[i].item, deserialized_history.items[i].item);
        TEST_ASSERT_EQUAL_INT(history.items[i].quantity, deserialized_history.items[i].quantity);
    }
    TEST_ASSERT_EQUAL_STRING(history.origin, deserialized_history.origin);
    TEST_ASSERT_EQUAL_STRING(history.destination, deserialized_history.destination);
    free(string);
}

void test_server_client_alive(void)
{
    server_client_alive alive = create_server_client_alive("user_test", "client", "active");
    char* string = serialize_server_client_alive(&alive);
    TEST_ASSERT_NOT_NULL(string);
    server_client_alive deserialized_alive = deserialize_server_client_alive(string);
    TEST_ASSERT_EQUAL_STRING(alive.type, deserialized_alive.type);
    TEST_ASSERT_EQUAL_STRING(alive.username, deserialized_alive.username);
    TEST_ASSERT_EQUAL_STRING(alive.client_type, deserialized_alive.client_type);
    TEST_ASSERT_EQUAL_STRING(alive.status, deserialized_alive.status);
    free(string);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_get_timestamp);
    RUN_TEST(test_validate_checksum_success);
    RUN_TEST(test_validate_checksum_failure);
    RUN_TEST(test_client_auth_request);
    RUN_TEST(test_server_auth_response);
    RUN_TEST(test_client_keepalive);
    RUN_TEST(test_client_inventory_update);
    RUN_TEST(test_server_emergency_alert);
    RUN_TEST(test_client_acknowledgment);
    RUN_TEST(test_client_infection_alert);
    RUN_TEST(test_server_w_stock_hub);
    RUN_TEST(test_warehouse_send_stock_to_hub);
    RUN_TEST(test_warehouse_request_stock);
    RUN_TEST(test_server_w_stock_warehouse);
    RUN_TEST(test_hub_request_stock);
    RUN_TEST(test_server_h_send_stock);
    RUN_TEST(test_cli_message);
    RUN_TEST(test_end_of_message);
    RUN_TEST(test_server_transaction_history);
    RUN_TEST(test_server_client_alive);

    return UNITY_END();
}
