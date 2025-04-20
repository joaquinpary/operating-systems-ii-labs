#ifndef BEHAVIOR_H
#define BEHAVIOR_H

#include "connection.h"
#include "json_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define WAREHOUSE "warehouse"
#define HUB "hub"
#define ACTIONS 2
#define MAX_ITEMS 6

typedef struct
{
    inventory_item items[MAX_ITEMS];
    inventory_item items_to_send[MAX_ITEMS];
    int warehouse_load_stock;
    int request_stock_from_hub;
    int hub_delivery;
    int hub_load_stock;
    int timer_tick;
    int next_action[ACTIONS];
} shared_data;

/* @brief
 * Function to authenticate the client with the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @return 0 on success, -1 on failure.
 */
int authenticate(init_params_client params, connection_context context);
/* @brief
 * Function to send a keepalive message to the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @return 0 on success, -1 on failure.
 */
int manager_sender(init_params_client params, connection_context context);
/* @brief
 * Function to receive a message from the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @return 0 on success, -1 on failure.
 */
int manager_receiver(init_params_client params, connection_context context);
/* @brief
 * Function to send a message to the server.
 * @param context The connection context.
 * @param protocol The protocol to use for sending the message.
 * @param buffer The message buffer to send.
 * @return 0 on success, -1 on failure.
 */
int sender(connection_context context, char* protocol, char* buffer);
/* @brief
 * Function to receive a message from the server.
 * @param context The connection context.
 * @param protocol The protocol to use for receiving the message.
 * @return The received message buffer.
 */
char* receiver(connection_context context, char* protocol);
/* @brief
 * Function to send a message to the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @param type_message The type of message to send.
 * @return 0 on success, -1 on failure.
 */
int message_sender(init_params_client params, connection_context context, int type_message);
/* @brief
 * Function to receive a message from the server.
 * @param response The response message buffer.
 * @return The received message buffer.
 */
int* message_receiver(char* response, init_params_client params, connection_context context);
/* @brief
 * Function to send a message to the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @param shm_ptr The shared memory pointer.
 * @param semid The semaphore ID.
 * @return 0 on success, 1 on failure.
 */
int warehouse_logic_sender(init_params_client params, connection_context context, shared_data* shm_ptr, int semid);
/* @brief
 * Function to receive a message from the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @param shm_ptr The shared memory pointer.
 * @param semid The semaphore ID.
 * @return 0 on success, 1 on failure.
 */
int warehouse_logic_receiver(init_params_client params, connection_context context, shared_data* shm_ptr, int semid);
/* @brief
 * Function to send a message to the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @param shm_ptr The shared memory pointer.
 * @param semid The semaphore ID.
 * @return 0 on success, 1 on failure.
 */
int hub_logic_sender(init_params_client params, connection_context context, shared_data* shm_ptr, int semid);
/* @brief
 * Function to receive a message from the server.
 * @param params The parameters for the client initialization.
 * @param context The connection context.
 * @param shm_ptr The shared memory pointer.
 * @param semid The semaphore ID.
 * @return 0 on success, 1 on failure.
 */
int hub_logic_receiver(init_params_client params, connection_context context, shared_data* shm_ptr, int semid);

#endif
