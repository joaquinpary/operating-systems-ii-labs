#define _POSIX_C_SOURCE 200809L

#include "behavior.h"
#include "config.h"
#include "connection.h"
#include "logger.h"
#include "shared_state.h"
#include "test_server.h"
#include "unity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define ACTIONS 2
#define SHM_SIZE 1024 // Segment size for shared memory (may be adjusted for a int)
#define SHM_KEY 50
#define SEM_KEY 51
#define SEM_WRITER 0
#define SEM_READER 1

#define SUCCESS "success"
#define FAILURE "failure"

#define ADD 1
#define SUBTRACT 0

// BOTH

#define NOTHING 0
#define REPLY 1

#define SERVER_AUTH_RESPONSE "server_auth_response"
#define SERVER_EMERGENCY_ALERT "server_emergency_alert"
#define INFECTION_ALERT "infection_alert"
#define ENEMY_THREAD "enemy_thread"
#define WEATHER_ALERT "weather_alert"

// TYPES MESSAGE
#define CLIENT_AUTH_REQUEST 1
#define CLIENT_KEEP_ALIVE 2
#define CLIENT_INVENTORY_UPDATE 3
#define CLIENT_ACK_SUCCESS 4
#define CLIENT_ACK_FAILURE 5
#define CLIENT_INFECTION_ALERT 6
// WAREHOUSE
#define SERVER_W_STOCK_HUB "server_w_stock_hub"
#define WAREHOUSE_SEND_STOCK_TO_HUB 7
#define WAREHOUSE_REQUEST_STOCK 8
#define WAREHOUSE_CANCELATION_ORDER 9 // to be implemented
#define SERVER_W_STOCK_WAREHOUSE "server_w_stock_warehouse"
// HUB
#define SERVER_H_REQUEST_DELIVERY "server_h_request_delivery"
#define HUB_REQUEST_STOCK 10
#define HUB_CANCELATION_ORDER 11 // to be implemented
#define SERVER_H_SEND_STOCK "server_h_send_stock"

// INTERNAL ACTIONS
#define WAREHOUSE_LOAD_STOCK 12
#define HUB_LOAD_STOCK 13

void setUp(void)
{
}
void tearDown(void)
{
}

void test_authenticate(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "user_test",
        .password = "pass_test",
        .client_id = "client_id_1",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    int result = authenticate(params, context);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
    sleep(1);
}

void test_authenticate_invalid(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "invalid_user",
        .password = "invalid_pass",
        .client_id = "client_id_2",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    int result = authenticate(params, context);
    TEST_ASSERT_EQUAL(1, result);
    close(context.sockfd);
    sleep(1);
}

void test_manager_sender_warehouse(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_sen_ware",
        .password = "test_pass",
        .client_id = "client_id_3",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    set_client_id(params.client_id);
    int sockfd = init_connection_tcp(params);
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    init_shared_memory();
    connection_context context = {.sockfd = sockfd};
    int result = manager_sender(params, context, 2, 1);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
    sleep(1);
}

void test_manager_sender_hub(void)
{
    init_params_client params = {
        .client_type = "hub",
        .username = "test_sen_hub",
        .password = "test_pass",
        .client_id = "client_id_3",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    set_client_id(params.client_id);
    int sockfd = init_connection_tcp(params);
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    init_shared_memory();
    connection_context context = {.sockfd = sockfd};
    int result = manager_sender(params, context, 2, 1);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
    sleep(1);
}

void test_manager_receiver_warehouse(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_man_recv",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    connection_context context = {.sockfd = 0};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    context.sockfd = init_connection_tcp(params);
    shared_data* shm_ptr = get_shared_data();
    int semid = get_semaphore_id();
    sem_wait();
    shm_ptr->items[0].quantity = 10;
    sem_signal();
    message_sender(params, context, WAREHOUSE_REQUEST_STOCK);
    int result = manager_receiver(params, context, 1);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
    sleep(1);
}

void test_manager_receiver_hub(void)
{
    init_params_client params = {
        .client_type = "hub",
        .username = "test_man_recv",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    connection_context context = {.sockfd = 0};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    context.sockfd = init_connection_tcp(params);
    shared_data* shm_ptr = get_shared_data();
    int semid = get_semaphore_id();
    sem_wait();
    shm_ptr->items[0].quantity = 3;
    sem_signal();
    message_sender(params, context, HUB_REQUEST_STOCK);
    int result = manager_receiver(params, context, 1);

    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
    sleep(1);
}

void test_message_sender_client_auth_request(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    client_auth_request auth_req = create_client_auth_request(params);
    char* serialized_request = serialize_client_auth_request(&auth_req);
    if (serialized_request == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing client_auth_request");
    }
    int result = message_sender(params, context, CLIENT_AUTH_REQUEST);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_client_keepalive(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    int result = message_sender(params, context, CLIENT_KEEP_ALIVE);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_client_inventory_update(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    connection_context context = {.sockfd = sockfd};
    int result = message_sender(params, context, CLIENT_INVENTORY_UPDATE);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_client_acknowledgment_success(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    int result = message_sender(params, context, CLIENT_ACK_SUCCESS);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_client_acknowledgment_failure(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    int result = message_sender(params, context, CLIENT_ACK_FAILURE);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_client_infection_alert(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    int result = message_sender(params, context, CLIENT_INFECTION_ALERT);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_warehouse_send_stock_to_hub(void)
{
    // start_test_server((server_config_t){.mode = SERVER_MODE_TCP, .family = SERVER_FAMILY_IPV4, .port = 9999});
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    set_inventory_to_send(get_inventory());
    int result = message_sender(params, context, WAREHOUSE_SEND_STOCK_TO_HUB);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_warehouse_request_stock(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    shared_data* shm_ptr = get_shared_data();
    int semid = get_semaphore_id();
    sem_wait();
    shm_ptr->items[0].quantity = 10;
    sem_signal();
    int result = message_sender(params, context, WAREHOUSE_REQUEST_STOCK);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_sender_hub_request_stock(void)
{
    init_params_client params = {
        .client_type = "hub",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    int sockfd = init_connection_tcp(params);
    connection_context context = {.sockfd = sockfd};
    set_client_id(params.client_id);
    set_client_type(params.client_type);
    set_session_token("test_session_token");
    init_shared_memory();
    shared_data* shm_ptr = get_shared_data();
    int semid = get_semaphore_id();
    sem_wait();
    shm_ptr->items[0].quantity = 2;
    sem_signal();
    int result = message_sender(params, context, HUB_REQUEST_STOCK);
    TEST_ASSERT_EQUAL(0, result);
    close(context.sockfd);
}

void test_message_receiver_server_auth_response_success(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    server_auth_response auth_res = create_server_auth_response("success", "token", "auth success");
    char* serialized_response = serialize_server_auth_response(&auth_res);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_auth_response");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_EQUAL(NOTHING, next_action[0]);
    TEST_ASSERT_EQUAL(NOTHING, next_action[1]);
    free(next_action);
}

void test_message_receiver_server_auth_response_failure(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    server_auth_response auth_res = create_server_auth_response("failure", "token", "auth failure");
    char* serialized_response = serialize_server_auth_response(&auth_res);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_auth_response");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_EQUAL(REPLY, next_action[0]);
    TEST_ASSERT_EQUAL(CLIENT_AUTH_REQUEST, next_action[1]);
    free(next_action);
}

void test_message_receiver_server_emergency_alert_infection_alert(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    server_emergency_alert emergency_alert = create_server_emergency_alert(INFECTION_ALERT);
    char* serialized_response = serialize_server_emergency_alert(&emergency_alert);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_emergency_alert");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_EQUAL(NOTHING, next_action[0]);
    TEST_ASSERT_EQUAL(NOTHING, next_action[1]);
    free(next_action);
}

void test_message_receiver_server_emergency_alert_enemy_thread(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    server_emergency_alert emergency_alert = create_server_emergency_alert(ENEMY_THREAD);
    char* serialized_response = serialize_server_emergency_alert(&emergency_alert);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_emergency_alert");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_EQUAL(NOTHING, next_action[0]);
    TEST_ASSERT_EQUAL(NOTHING, next_action[1]);
    free(next_action);
}

void test_message_receiver_server_emergency_alert_wather_alert(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    server_emergency_alert emergency_alert = create_server_emergency_alert(WEATHER_ALERT);
    char* serialized_response = serialize_server_emergency_alert(&emergency_alert);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_emergency_alert");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_EQUAL(NOTHING, next_action[0]);
    TEST_ASSERT_EQUAL(NOTHING, next_action[1]);
    free(next_action);
}
void test_message_receiver_server_warehouse_stock_hub()
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    inventory_item items[6] = {{"water", 10}, {"food", 5}, {"medicine", 7}, {"guns", 1}, {"ammo", 3}, {"tools", 2}};
    server_w_stock_hub send_stock = create_server_w_stock_hub(items, 6);
    char* serialized_response = serialize_server_w_stock_hub(&send_stock, 6);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_w_stock_hub");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_NOT_NULL(next_action);
    free(next_action);
}

void test_message_receiver_server_warehouse_stock_warehouse(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    inventory_item items[6] = {{"water", 10}, {"food", 5}, {"medicine", 7}, {"guns", 1}, {"ammo", 3}, {"tools", 2}};
    server_w_stock_warehouse send_stock = create_server_w_stock_warehouse(items, 6);
    char* serialized_response = serialize_server_w_stock_warehouse(&send_stock, 6);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing warehouse_send_stock_to_hub");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_NOT_NULL(next_action);
    free(next_action);
}

void test_message_receiver_server_hub_send_stock(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    inventory_item items[6] = {{"water", 10}, {"food", 5}, {"medicine", 7}, {"guns", 1}, {"ammo", 3}, {"tools", 2}};
    server_h_send_stock send_stock = create_server_h_send_stock(items, 6);
    char* serialized_response = serialize_server_h_send_stock(&send_stock, 6);
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing hub_send_stock");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_NOT_NULL(next_action);
    free(next_action);
}

void test_message_receiver_unknown_type(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};
    char* serialized_response = strdup(
        "{\"type\":\"unknown_type\",\"payload\":{\"items\":[{\"item\":\"water\",\"quantity\":100},{\"item\":\"food\","
        "\"quantity\":100},{\"item\":\"medicine\",\"quantity\":100},{\"item\":\"guns\",\"quantity\":100},{\"item\":"
        "\"ammo\",\"quantity\":100},{\"item\":\"tools\",\"quantity\":100}],\"timestamp\":\"2025-04-17T03:17:14Z\"},"
        "\"checksum\":\"AD71E586\"}");
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_NULL(next_action);
    free(next_action);
}

void test_message_receiver_checksum_error(void)
{
    init_params_client params = {
        .client_type = "warehouse",
        .username = "test_user",
        .password = "test_pass",
        .client_id = "client_id",
        .connection_params = {.host = "localhost", .port = "9999", .protocol = "tcp", .ip_version = "ipv4"}};

    char* serialized_response = strdup(
        "{\"type\":\"server_w_stock_hub\",\"payload\":{\"items\":[{\"item\":\"water\",\"quantity\":100},{\"item\":"
        "\"food\",\"quantity\":100},{\"item\":\"medicine\",\"quantity\":100},{\"item\":\"guns\",\"quantity\":100},{"
        "\"item\":\"ammo\",\"quantity\":100},{\"item\":\"tools\",\"quantity\":100}],\"timestamp\":\"2025-04-17T03:17:"
        "14Z\"},\"checksum\":\"1826DB22\"}");
    if (serialized_response == NULL)
    {
        TEST_FAIL_MESSAGE("Error serializing server_auth_response");
    }
    connection_context context = {.sockfd = 0};
    int* next_action = message_receiver(serialized_response, params, context);
    TEST_ASSERT_NULL(next_action);
}

int main(void)
{
    test_server_t test_server;
    start_test_server(&test_server,
                      (server_config_t){.mode = SERVER_MODE_TCP, .family = SERVER_FAMILY_IPV4, .port = 9999});
    UNITY_BEGIN();
    RUN_TEST(test_authenticate);
    RUN_TEST(test_authenticate_invalid);
    RUN_TEST(test_manager_sender_warehouse);
    RUN_TEST(test_manager_sender_hub);
    RUN_TEST(test_manager_receiver_warehouse);
    RUN_TEST(test_manager_receiver_hub);
    RUN_TEST(test_message_sender_client_auth_request);
    RUN_TEST(test_message_sender_client_keepalive);
    RUN_TEST(test_message_sender_client_inventory_update);
    RUN_TEST(test_message_sender_client_acknowledgment_success);
    RUN_TEST(test_message_sender_client_acknowledgment_failure);
    RUN_TEST(test_message_sender_client_infection_alert);
    RUN_TEST(test_message_sender_warehouse_send_stock_to_hub);
    RUN_TEST(test_message_sender_warehouse_request_stock);
    RUN_TEST(test_message_sender_hub_request_stock);
    RUN_TEST(test_message_receiver_server_auth_response_success);
    RUN_TEST(test_message_receiver_server_auth_response_failure);
    RUN_TEST(test_message_receiver_server_emergency_alert_infection_alert);
    RUN_TEST(test_message_receiver_server_emergency_alert_enemy_thread);
    RUN_TEST(test_message_receiver_server_emergency_alert_wather_alert);
    RUN_TEST(test_message_receiver_server_warehouse_stock_hub);
    RUN_TEST(test_message_receiver_server_warehouse_stock_warehouse);
    RUN_TEST(test_message_receiver_server_hub_send_stock);
    RUN_TEST(test_message_receiver_unknown_type);
    RUN_TEST(test_message_receiver_checksum_error);
    int result = UNITY_END();
    stop_test_server(&test_server);
    return result;
}
