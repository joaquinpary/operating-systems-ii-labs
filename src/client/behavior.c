#include "behavior.h"
#include "config.h"
#include "connection.h"
#include "inventory.h"
#include "json_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <unistd.h>

#define ACTIONS 2

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
#define HUB_RECEIVE_STOCK 12 


// INTERNAL ACTIONS
#define WAREHOUSE_LOAD_STOCK 12
#define HUB_LOAD_STOCK 13

int authenticate(connection_context context)
{
    int* next_action = NULL;
    char* response = NULL;

    for (int i = 0; i < 3; i++)
    {
        if (message_sender(context, CLIENT_AUTH_REQUEST))
            return 1;

        response = receiver(context, get_identifiers()->protocol);
        if (response == NULL)
        {
            log_error("Error receiving response with ID: %s", get_identifiers()->client_id);
            return 1;
        }

        next_action = message_receiver(response, context);
        if (next_action == NULL)
        {
            log_error("Error processing response with ID: %s", get_identifiers()->client_id);
            return 1;
        }

        if (next_action[0] == NOTHING)
        {
            free(next_action);
            return 0;
        }
        free(next_action);
    }
    log_info("Authentication failed after 3 attempts with ID: %s", get_identifiers()->client_id);

    return 1;
}

int manager_sender(connection_context context, int time, int finish)
{
    if (!strcmp(get_identifiers()->client_type, WAREHOUSE))
    {
        if (warehouse_logic_sender(context, time, finish))
            return 1;
    }
    else if (!strcmp(get_identifiers()->client_type, HUB))
    {
        if (hub_logic_sender(context, time, finish))
            return 1;
    }
    else
    {
        log_error("Invalid client type with ID: %s", get_identifiers()->client_id);
        return 1;
    }
    return 0;
}

int manager_receiver(connection_context context, int finish)
{
    if (!strcmp(get_identifiers()->client_type, WAREHOUSE))
    {
        if (warehouse_logic_receiver(context, finish))
            return 1;
    }
    else if (!strcmp(get_identifiers()->client_type, HUB))
    {
        if (hub_logic_receiver(context, finish))
            return 1;
    }
    else
    {
        fprintf(stderr, "Invalid client type\n");
        return 1;
    }
    return 0;
}

int sender(connection_context context, char* protocol, char* buffer)
{
    if (!strcmp(protocol, UDP))
    {
        if (sender_udp(context.sockfd, buffer, &context.dest_addr, context.addr_len))
            return 1;
    }
    else if (!strcmp(protocol, TCP))
    {
        if (sender_tpc(context.sockfd, buffer))
            return 1;
    }
    return 0;
}

char* receiver(connection_context context, char* protocol)
{

    char* buffer = malloc(SOCKET_SIZE);
    if (!buffer)
    {
        fprintf(stderr, "malloc failed\n");
        return NULL;
    }

    if (!strcmp(protocol, UDP))
    {
        memset(buffer, 0, SOCKET_SIZE);
        if (receiver_udp(context.sockfd, buffer, &context.dest_addr, context.addr_len))
        {
            free(buffer);
            return NULL;
        }
    }
    else if (!strcmp(protocol, TCP))
    {
        memset(buffer, 0, SOCKET_SIZE);
        if (receiver_tcp(context.sockfd, buffer))
        {
            free(buffer);
            return NULL;
        }
    }
    return buffer;
}

int message_sender(connection_context context, int type_message)
{
    char* buffer = NULL;
    switch (type_message)
    {
    case CLIENT_AUTH_REQUEST:
        client_auth_request auth_req =
            create_client_auth_request(get_identifiers()->client_id, get_identifiers()->client_type,
                                       get_identifiers()->username, get_identifiers()->password);
        buffer = serialize_client_auth_request(&auth_req);
        if (buffer == NULL)
        {
            log_error("Error serializing client_auth_request");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case CLIENT_KEEP_ALIVE:
        client_keepalive keep_alive =
            create_client_keepalive(get_identifiers()->username, get_identifiers()->session_token);
        buffer = serialize_client_keepalive(&keep_alive);
        if (buffer == NULL)
        {
            log_error("Error serializing client_keepalive");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case CLIENT_INVENTORY_UPDATE:
        client_inventory_update inv_upd = create_client_inventory_update(
            get_identifiers()->username, get_identifiers()->session_token, get_inventory(), get_inventory_size());
        buffer = serialize_client_inventory_update(&inv_upd, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing client_inventory_update");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case CLIENT_ACK_SUCCESS:
        client_acknowledgment ack =
            create_client_acknowledgment(get_identifiers()->username, get_identifiers()->session_token, SUCCESS);
        buffer = serialize_client_acknowledgment(&ack);
        if (buffer == NULL)
        {
            log_error("Error serializing client_acknowledgment");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case CLIENT_ACK_FAILURE:
        client_acknowledgment ack_fail =
            create_client_acknowledgment(get_identifiers()->username, get_identifiers()->session_token, FAILURE);
        buffer = serialize_client_acknowledgment(&ack_fail);
        if (buffer == NULL)
        {
            log_error("Error serializing client_acknowledgment");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case CLIENT_INFECTION_ALERT:
        client_emergency_alert infec_alert =
            create_client_infection_alert(get_identifiers()->username, get_identifiers()->session_token);
        buffer = serialize_client_infection_alert(&infec_alert);
        if (buffer == NULL)
        {
            log_error("Error serializing client_emergency_alert");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case WAREHOUSE_SEND_STOCK_TO_HUB:
        warehouse_send_stock_to_hub send_stock_hub =
            create_warehouse_send_stock_to_hub(get_identifiers()->username, get_identifiers()->session_token, get_hub_username(),
                                               get_inventory_to_send(), get_inventory_size());
        buffer = serialize_warehouse_send_stock_to_hub(&send_stock_hub, get_inventory_size());
        if (buffer == NULL)
        {
            fprintf(stderr, "Error serializing warehouse_send_stock_to_hub\n");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case WAREHOUSE_REQUEST_STOCK:
        warehouse_request_stock restock_warehouse =
            create_warehouse_request_stock(get_identifiers()->username, get_identifiers()->session_token,
                                           get_inventory_to_replenish(), get_inventory_size());
        buffer = serialize_warehouse_request_stock(&restock_warehouse, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing warehouse_request_stock");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case HUB_REQUEST_STOCK:
        hub_request_stock restock_hub =
            create_hub_request_stock(get_identifiers()->username, get_identifiers()->session_token,
                                     get_inventory_to_replenish(), get_inventory_size());
        buffer = serialize_hub_request_stock(&restock_hub, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing warehouse_request_stock");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    case HUB_RECEIVE_STOCK:
        client_acknowledgment hub_receive = create_hub_receive_stock(
            get_identifiers()->username, get_identifiers()->session_token, SUCCESS);
        buffer = serialize_client_acknowledgment(&hub_receive);
        if (buffer == NULL)
        {
            log_error("Error serializing client_acknowledgment");
            return 1;
        }
        if (sender(context, get_identifiers()->protocol, buffer))
        {
            free(buffer);
            return 1;
        }
        free(buffer);
        break;
    default:
        log_error("Unknown message type to send with ID : %s", get_identifiers()->client_id);
        return 1;
        break;
    }
    return 0;
}

int* message_receiver(char* response, connection_context context)
{
    int* next_action = malloc(ACTIONS * sizeof(int));
    if (next_action == NULL)
    {
        log_error("Error allocating memory for next_action");
        return NULL;
    }
    char* type = get_type(response);
    if (type == NULL)
    {
        log_error("Error getting message type with ID: %s", get_identifiers()->client_id);
        free(response);
        return NULL;
    }
    if (strcmp(type, SERVER_AUTH_RESPONSE))
    {
        if (validate_checksum(response))
        {
            log_error("Checksum error with ID: %s", get_identifiers()->client_id);
            if (message_sender(context, CLIENT_ACK_FAILURE))
            {
                log_error("Error sending acknowledgment\n");
                free(type);
                free(response);
                return NULL;
            }
            log_info("Client finished with ID: %s", get_identifiers()->client_id);
            free(type);
            free(response);
            return NULL;
        }
        else
        {
            if (message_sender(context, CLIENT_ACK_SUCCESS))
            {
                log_error("Error sending acknowledgment\n");
                free(type);
                free(response);
                return NULL;
            }
        }
    }
    if (!strcmp(type, SERVER_AUTH_RESPONSE))
    {
        printf("Server auth response received\n");
        server_auth_response auth_res = deserialize_server_auth_response(response);
        if (!strcmp(auth_res.payload.status, SUCCESS))
        {
            log_info("Authentication successful with ID: %s", get_identifiers()->client_id);
            next_action[0] = NOTHING;
            next_action[1] = NOTHING;
            set_session_token(auth_res.payload.session_token);
        }
        else
        {
            log_info("Authentication failed with ID: %s", get_identifiers()->client_id);
            next_action[0] = REPLY;
            next_action[1] = CLIENT_AUTH_REQUEST;
            log_info("Trying to reauthenticate with ID: %s", get_identifiers()->client_id);
        }
        free(response);
        free(type);
        return next_action;
    }
    else if (!strcmp(type, SERVER_EMERGENCY_ALERT))
    {
        server_emergency_alert emerg_alert = deserialize_server_emergency_alert(response);
        next_action[0] = NOTHING;
        next_action[1] = NOTHING;
        if (!strcmp(emerg_alert.payload.alert_type, INFECTION_ALERT))
        {
            log_warning("Emergency alert [INFECTION] received with ID: %s", get_identifiers()->client_id);
        }
        else if (!strcmp(emerg_alert.payload.alert_type, ENEMY_THREAD))
        {
            log_warning("Emergency alert [ENEMY THREAD] received with ID: %s", get_identifiers()->client_id);
        }
        else if (!strcmp(emerg_alert.payload.alert_type, WEATHER_ALERT))
        {
            log_warning("Emergency alert [WEATHER ALERT] received with ID: %s", get_identifiers()->client_id);
        }
        free(type);
        free(response);
        return next_action;
    }
    else if (!strcmp(type, SERVER_W_STOCK_HUB))
    {
        server_w_stock_hub stock_hub = deserialize_server_w_stock_hub(response);
        set_inventory_to_send(stock_hub.payload.items);
        set_hub_username(stock_hub.payload.hub_username);
        next_action[0] = REPLY;
        next_action[1] = WAREHOUSE_SEND_STOCK_TO_HUB;
        free(type);
        free(response);
        return next_action;
    }
    else if (!strcmp(type, SERVER_W_STOCK_WAREHOUSE))
    {
        server_w_stock_warehouse stock_warehouse = deserialize_server_w_stock_warehouse(response);
        set_inventory(stock_warehouse.payload.items);
        log_info("WAREHOUSE stock updated with ID: %s", get_identifiers()->client_id);
        next_action[0] = NOTHING;
        next_action[1] = WAREHOUSE_LOAD_STOCK;
        free(type);
        free(response);
        return next_action;
    }
    else if (!strcmp(type, SERVER_H_SEND_STOCK))
    {
        server_h_send_stock stock_hub = deserialize_server_h_send_stock(response);
        set_inventory(stock_hub.payload.items);
        log_info("HUB stock updated with ID: %s", get_identifiers()->client_id);
        if(message_sender(context, HUB_RECEIVE_STOCK))
        {
            log_error("Error sending acknowledgment\n");
            free(type);
            free(response);
            return NULL;
        }
        next_action[0] = NOTHING;
        next_action[1] = HUB_LOAD_STOCK;
        free(type);
        free(response);
        return next_action;
    }
    else
    {
        fprintf(stderr, "Unknown message type received\n");
    }
    if (type)
        free(type);
    if (response)
        free(response);
    if (next_action)
        free(next_action);
    return NULL;
}

int warehouse_logic_sender(connection_context context, int time, int finish)
{
    int infection;
    while (1)
    {

        if (get_uniform_random(0, 100) < 1)
        {
            if (message_sender(context, CLIENT_INFECTION_ALERT))
                return 1;
            log_info("Infection alert sent with ID: %s", get_identifiers()->client_id);
        }
        if (replenish())
        {
            log_info("Low inventory detected, sending request for supply to server with ID: %s",
                     get_identifiers()->client_id);
            if (message_sender(context, WAREHOUSE_REQUEST_STOCK))
                return 1;
        }

        if (message_sender(context, CLIENT_KEEP_ALIVE))
            return 1;
        if (message_sender(context, CLIENT_INVENTORY_UPDATE))
            return 1;
        if (finish)
            break;
        sleep(60);
        if (replenish())
        {
            log_info("Low inventory detected, sending request for supply to server with ID: %s",
                     get_identifiers()->client_id);
            if (message_sender(context, WAREHOUSE_REQUEST_STOCK))
                return 1;
        }
    }
    return 0;
}

int warehouse_logic_receiver(connection_context context, int finish)
{
    char* recv = NULL;
    while (1)
    {
        recv = receiver(context, get_identifiers()->protocol);
        if (recv == NULL)
        {
            fprintf(stderr, "Error receiving response\n");
            return 1;
        }
        int* next_action = message_receiver(recv, context);
        if (next_action[0] == REPLY)
        {
            next_action[0] = NOTHING;
            if (message_sender(context, next_action[1]))
                return 1;
        }
        if (finish)
            break;
    }
    return 0;
}

int hub_logic_sender(connection_context context, int time, int finish)
{
    int count = 60;
    int next_compsumption = get_uniform_random(7, 13);
    int emergency;
    while (1)
    {
        if (--next_compsumption <= 0)
        {
            if (inventory_compsumption())
                return 1;
            next_compsumption = (int)get_uniform_random(7, 13);
            log_info("Inventory consumption with ID: %s", get_identifiers()->client_id);
        }
        if (count >= time)
        {
            count = 0;
            if (get_uniform_random(0, 100) < 1)
            {
                if (message_sender(context, CLIENT_INFECTION_ALERT))
                    return 1;
                log_info("Infection alert sent with ID: %s", get_identifiers()->client_id);
            }
            if (replenish())
            {
                log_info("Low inventory detected, sending request for supply to server with ID: %s",
                         get_identifiers()->client_id);
                if (message_sender(context, HUB_REQUEST_STOCK))
                    return 1;
            }
            if (message_sender(context, CLIENT_KEEP_ALIVE))
                return 1;
            if (message_sender(context, CLIENT_INVENTORY_UPDATE))
                return 1;
            if (finish)
                break;
        }
        sleep(1);
        count++;
    }
    return 0;
}

int hub_logic_receiver(connection_context context, int finish)
{
    char* recv = NULL;
    while (1)
    {
        recv = receiver(context, get_identifiers()->protocol);
        if (recv == NULL)
        {
            fprintf(stderr, "Error receiving response\n");
            return 1;
        }
        int* next_action = message_receiver(recv, context);
        if (next_action[0] == REPLY)
        {
            next_action[0] = NOTHING;
            if (message_sender(context, next_action[1]))
                return 1;
        }
        if (finish)
            break;
    }
    return 0;
}
