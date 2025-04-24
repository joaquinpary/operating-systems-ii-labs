#include "behavior.h"
#include "config.h"
#include "connection.h"
#include "inventory.h"
#include "json_manager.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
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

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

void sem_wait(int semid, int semnum)
{
    struct sembuf sb = {semnum, -1, 0};
    semop(semid, &sb, 1);
}

void sem_signal(int semid, int semnum)
{
    struct sembuf sb = {semnum, 1, 0};
    semop(semid, &sb, 1);
}

int authenticate(init_params_client params, connection_context context)
{
    int* next_action = NULL;
    char* response = NULL;

    for (int i = 0; i < 3; i++)
    {
        message_sender(params, context, CLIENT_AUTH_REQUEST);

        response = receiver(context, params.connection_params.protocol);
        if (response == NULL)
        {
            log_error("Error receiving response with ID: %s", get_identifiers()->client_id);
            return 1;
        }

        next_action = message_receiver(response, params, context);
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

int manager_sender(init_params_client params, connection_context context)
{
    key_t key = ftok(get_identifiers()->shm_path, SHM_KEY); // Crea una clave única
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);    // Crea el segmento de memoria
    if (shmid == -1)
    {
        perror("Error to create shared memory");
        return 1;
    }
    // Adjunta la memoria compartida al espacio de direcciones del proceso
    shared_data* shm_ptr = (shared_data*)shmat(shmid, NULL, 0);
    if (shm_ptr == (shared_data*)-1)
    {
        perror("Error to attach shared memory");
        return 1;
    }
    key_t sem_key = ftok(get_identifiers()->shm_path, SEM_KEY);
    int semid = semget(sem_key, 2, 0666 | IPC_CREAT);
    if (semid == -1)
    {
        perror("semget failed");
        exit(1);
    }
    // Inicializar semáforo 0 si está en 0
    union semun sem_union;
    if (semctl(semid, 0, GETVAL) == 0)
    {
        sem_union.val = 1;
        if (semctl(semid, 0, SETVAL, sem_union) == -1)
        {
            perror("semctl failed to set semval");
            return 1;
        }
    }
    sem_wait(semid, SEM_WRITER);
    memset(shm_ptr, 0, sizeof(shared_data));
    sem_signal(semid, SEM_WRITER);
    if (!strcmp(params.client_type, WAREHOUSE))
    {
        warehouse_logic_sender(params, context, shm_ptr, semid);
    }
    else if (!strcmp(params.client_type, HUB))
    {
        hub_logic_sender(params, context, shm_ptr, semid);
    }
    else
    {
        log_error("Invalid client type with ID: %s", get_identifiers()->client_id);
        return 1;
    }
    shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}

int manager_receiver(init_params_client params, connection_context context)
{
    key_t key = ftok(get_identifiers()->shm_path, SHM_KEY);
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    if (shmid == -1)
    {
        perror("Error to get shared memory");
        return 1;
    }
    shared_data* shm_ptr = (shared_data*)shmat(shmid, NULL, 0);
    if (shm_ptr == (void*)-1)
    {
        perror("Error attaching shared memory");
        return 1;
    }
    key_t sem_key = ftok(get_identifiers()->shm_path, SEM_KEY);
    int semid = semget(sem_key, 2, 0666 | IPC_CREAT);
    if (semid == -1)
    {
        perror("semget failed");
        shmdt(shm_ptr);
        return 1;
    }

    if (!strcmp(params.client_type, WAREHOUSE))
    {
        warehouse_logic_receiver(params, context, shm_ptr, semid);
    }
    else if (!strcmp(params.client_type, HUB))
    {
        hub_logic_receiver(params, context, shm_ptr, semid);
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
    int bytes_send;
    if (!strcmp(protocol, UDP))
    {
        bytes_send = sender_udp(context.sockfd, buffer, &context.dest_addr, context.addr_len);
    }
    else if (!strcmp(protocol, TCP))
    {
        bytes_send = sender_tpc(context.sockfd, buffer);
    }
    return bytes_send;
}

char* receiver(connection_context context, char* protocol)
{
    int bytes_recv;
    char* buffer = malloc(SOCKET_SIZE);
    if (!buffer)
    {
        fprintf(stderr, "malloc failed\n");
        return NULL;
    }

    if (!strcmp(protocol, UDP))
    {
        memset(buffer, 0, SOCKET_SIZE);
        bytes_recv = receiver_udp(context.sockfd, buffer, &context.dest_addr, context.addr_len);
    }
    else if (!strcmp(protocol, TCP))
    {
        memset(buffer, 0, SOCKET_SIZE);
        bytes_recv = receiver_tcp(context.sockfd, buffer);
    }

    if (bytes_recv < 0)
    {
        free(buffer);
        return NULL;
    }

    return buffer;
}

int message_sender(init_params_client params, connection_context context, int type_message)
{
    int bytes_send;
    char* buffer = NULL;
    int inventory_size = get_inventory_size();
    inventory_item* items_to_replenish = malloc(inventory_size * sizeof(inventory_item));
    if (items_to_replenish == NULL)
    {
        log_error("Error allocating memory for items");
        return 1;
    }
    switch (type_message)
    {
    case CLIENT_AUTH_REQUEST:
        client_auth_request auth_req = create_client_auth_request(params);
        buffer = serialize_client_auth_request(&auth_req);
        if (buffer == NULL)
        {
            log_error("Error serializing client_auth_request");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_auth_request");
            return 1;
        }
        break;
    case CLIENT_KEEP_ALIVE:
        client_keepalive keep_alive = create_client_keepalive(params.username, get_identifiers()->session_token);
        buffer = serialize_client_keepalive(&keep_alive);
        if (buffer == NULL)
        {
            log_error("Error serializing client_keepalive");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_keepalive");
            return 1;
        }
        break;
    case CLIENT_INVENTORY_UPDATE:
        inventory_item* items = get_full_inventory();
        client_inventory_update inv_upd = create_client_inventory_update(
            params.username, get_identifiers()->session_token, items, get_inventory_size());
        buffer = serialize_client_inventory_update(&inv_upd, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing client_inventory_update");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        sleep(1);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_inventory_update");

            return 1;
        }
        break;
    case CLIENT_ACK_SUCCESS:
        client_acknowledgment ack =
            create_client_acknowledgment(params.username, get_identifiers()->session_token, SUCCESS);
        buffer = serialize_client_acknowledgment(&ack);
        if (buffer == NULL)
        {
            log_error("Error serializing client_acknowledgment");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_acknowledgment");
            return 1;
        }
        break;
    case CLIENT_ACK_FAILURE:
        client_acknowledgment ack_fail =
            create_client_acknowledgment(params.username, get_identifiers()->session_token, FAILURE);
        buffer = serialize_client_acknowledgment(&ack_fail);
        if (buffer == NULL)
        {
            log_error("Error serializing client_acknowledgment");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_acknowledgment");
            return 1;
        }
        break;
    case CLIENT_INFECTION_ALERT:
        client_infection_alert infec_alert =
            create_client_infection_alert(params.username, get_identifiers()->session_token);
        buffer = serialize_client_infection_alert(&infec_alert);
        if (buffer == NULL)
        {
            log_error("Error serializing client_infection_alert");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            log_error("Error sending client_infection_alert");
            return 1;
        }
        break;
    case WAREHOUSE_SEND_STOCK_TO_HUB:
        warehouse_send_stock_to_hub send_stock_hub = create_warehouse_send_stock_to_hub(
            params.username, get_identifiers()->session_token, get_full_inventory_to_send(), get_inventory_size());
        buffer = serialize_warehouse_send_stock_to_hub(&send_stock_hub, get_inventory_size());
        if (buffer == NULL)
        {
            fprintf(stderr, "Error serializing warehouse_send_stock_to_hub\n");
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        if (bytes_send < 0)
        {
            fprintf(stderr, "Error sending warehouse_send_stock_to_hub\n");
            free(buffer);
            return 1;
        }
        break;
    case WAREHOUSE_REQUEST_STOCK:
        get_items_to_replenish(items_to_replenish);
        warehouse_request_stock restock_warehouse = create_warehouse_request_stock(
            params.username, get_identifiers()->session_token, items_to_replenish, get_inventory_size());
        buffer = serialize_warehouse_request_stock(&restock_warehouse, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing warehouse_request_stock");
            free(items_to_replenish);
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        free(items_to_replenish);
        if (bytes_send < 0)
        {
            log_error("Error sending warehouse_request_stock");
            return 1;
        }
        break;
    case HUB_REQUEST_STOCK:
        get_items_to_replenish(items_to_replenish);
        hub_request_stock restock_hub = create_hub_request_stock(params.username, get_identifiers()->session_token,
                                                                 items_to_replenish, get_inventory_size());
        buffer = serialize_hub_request_stock(&restock_hub, get_inventory_size());
        if (buffer == NULL)
        {
            log_error("Error serializing warehouse_request_stock");
            free(items_to_replenish);
            return 1;
        }
        bytes_send = sender(context, params.connection_params.protocol, buffer);
        free(buffer);
        free(items_to_replenish);
        if (bytes_send < 0)
        {
            log_error("Error sending warehouse_request_stock");
            return 1;
        }
        // Verificar
        break;
    default:
        log_error("Unknown message type to send with ID : %s", get_identifiers()->client_id);
        break;
    }
    return bytes_send;
}

int* message_receiver(char* response, init_params_client params, connection_context context)
{
    int bytes_send;
    int* next_action = malloc(ACTIONS * sizeof(int));
    char* type = get_type(response);
    int val_check = validate_checksum(response);
    if (strcmp(type, SERVER_AUTH_RESPONSE))
    {
        if (val_check)
        {
            log_error("Checksum error with ID: %s", get_identifiers()->client_id);
            bytes_send = message_sender(params, context, CLIENT_ACK_FAILURE);
            if (bytes_send < 0)
            {
                log_error("Error sending acknowledgment\n");
                free(type);
                free(response);
                return NULL;
            }
            log_info("Client finished with ID: %s", get_identifiers()->client_id);
            free(type);
            free(response);
            next_action[0] = NOTHING;
            next_action[1] = NOTHING;
            return next_action;
        }
        else
        {
            bytes_send = message_sender(params, context, CLIENT_ACK_SUCCESS);
            if (bytes_send < 0)
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
        // Revisar pero parece que esta implementado
    }
    else if (!strcmp(type, SERVER_W_STOCK_HUB))
    {
        server_w_stock_hub stock_hub = deserialize_server_w_stock_hub(response);
        set_item_quantity_to_send(stock_hub.payload.items);
        next_action[0] = REPLY;
        next_action[1] = WAREHOUSE_SEND_STOCK_TO_HUB;
        return next_action;
        // REVISAR
    }
    else if (!strcmp(type, SERVER_W_STOCK_WAREHOUSE))
    {
        server_w_stock_warehouse stock_warehouse = deserialize_server_w_stock_warehouse(response);
        set_item_quantity(stock_warehouse.payload.items, ADD);
        log_info("WAREHOUSE stock updated with ID: %s", get_identifiers()->client_id);
        next_action[0] = NOTHING;
        next_action[1] = WAREHOUSE_LOAD_STOCK;
        free(type);
        free(response);
        return next_action;
        // REVISAR
    }
    else if (!strcmp(type, SERVER_H_REQUEST_DELIVERY))
    {
        // IMPLEMENTAR AL FINAL
        // hacer nuevo tipo de struct
        //  PARA EL HUB
        //  Implementar la logica de delivery request
        //  Pedido del server de entregar paquetes
    }
    else if (!strcmp(type, SERVER_H_SEND_STOCK))
    {
        server_h_send_stock stock_hub = deserialize_server_h_send_stock(response);
        set_item_quantity(stock_hub.payload.items, ADD);
        log_info("HUB stock updated with ID: %s", get_identifiers()->client_id);
        next_action[0] = NOTHING;
        next_action[1] = NOTHING;
        return next_action;
        // hacer nuevo tipo de struct
        //  PARA EL HUB
        //  Implementar la logica de restock hub
        //  Aviso del server de que el warehouse le va a enviar stock
    }
    else
    {
        fprintf(stderr, "Unknown message type received\n");
    }
    return NULL;
}

int warehouse_logic_sender(init_params_client params, connection_context context, shared_data* shm_ptr, int semid)
{
    int bytes_send;
    pid_t pid = fork();

    if (pid == 0)
    {
        while (1)
        {
            sleep(1);
            sem_wait(semid, SEM_WRITER);
            shm_ptr->timer_tick++;
            // printf("Timer tick: %d\n", shm_ptr->timer_tick);
            sem_signal(semid, SEM_WRITER);
        }
        exit(EXIT_SUCCESS);
    }
    else if (pid > 0)
    {
        connection_context context_warehouse = context;
        int timer_tick = 0;
        int request_stock = 0;
        int warehouse_load_stock = 0;
        int request_stock_from_hub = 0;
        int* next_action = malloc(ACTIONS * sizeof(int));
        if (next_action == NULL)
        {
            fprintf(stderr, "Error allocating memory for next_action\n");
            shmdt(shm_ptr);
            return 1;
        }
        inventory_item* items = malloc(sizeof(inventory_item) * get_inventory_size());
        if (items == NULL)
        {
            fprintf(stderr, "Error allocating memory for items\n");
            shmdt(shm_ptr);
            return 1;
        }
        inventory_item* items_to_send = malloc(sizeof(inventory_item) * get_inventory_size());
        if (items_to_send == NULL)
        {
            fprintf(stderr, "Error allocating memory for items_to_send\n");
            free(items);
            shmdt(shm_ptr);
            return 1;
        }
        while (1)
        {
            sem_wait(semid, SEM_WRITER);
            timer_tick = shm_ptr->timer_tick;
            memcpy(items, shm_ptr->items, sizeof(inventory_item) * get_inventory_size());
            memcpy(items_to_send, shm_ptr->items_to_send, sizeof(inventory_item) * get_inventory_size());
            warehouse_load_stock = shm_ptr->warehouse_load_stock;
            request_stock_from_hub = shm_ptr->request_stock_from_hub;
            next_action[0] = shm_ptr->next_action[0];
            next_action[1] = shm_ptr->next_action[1];
            if (shm_ptr->timer_tick == 60)
                shm_ptr->timer_tick = 0;
            sem_signal(semid, SEM_WRITER);
            if (compare_inventory(items) && warehouse_load_stock)
            {
                printf("Inventory updated. Sending to server...\n");
                set_item_quantity(items, ADD);
                log_info("WAREHOUSE stock updated with ID: %s", get_identifiers()->client_id);
                sem_wait(semid, SEM_WRITER);
                shm_ptr->warehouse_load_stock = 0;
                sem_signal(semid, SEM_WRITER);
                request_stock = 0;
            };
            if (!verify_inventory() && !request_stock)
            {
                log_info("Low inventory detected, sending request for supply to server with ID: %s",
                         get_identifiers()->client_id);
                bytes_send = message_sender(params, context_warehouse, WAREHOUSE_REQUEST_STOCK);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending request for supply\n");
                    shmdt(shm_ptr);
                    return 1;
                }
                request_stock = 1;
            }

            if (request_stock_from_hub)
            {
                set_item_quantity_to_send(items_to_send);
                set_item_quantity(items_to_send, SUBTRACT);
                sem_wait(semid, SEM_WRITER);
                shm_ptr->request_stock_from_hub = 0;
                sem_signal(semid, SEM_WRITER);
            }
            if (timer_tick == 60)
            {
                // printf("Timer activated. Executing warehouse logic...\n");
                // printf("Timer tick: %d\n", timer_tick);
                bytes_send = message_sender(params, context_warehouse, CLIENT_KEEP_ALIVE);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending keepalive\n");
                    shmdt(shm_ptr);
                    return 1;
                }
                bytes_send = message_sender(params, context_warehouse, CLIENT_INVENTORY_UPDATE);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending inventory update\n");
                    shmdt(shm_ptr);
                    return 1;
                }
            }

            if (next_action[0] == REPLY)
            {
                sem_wait(semid, SEM_WRITER);
                shm_ptr->next_action[0] = NOTHING;
                sem_signal(semid, SEM_WRITER);
                bytes_send = message_sender(params, context_warehouse, next_action[1]);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending inventory update\n");
                    sem_signal(semid, SEM_WRITER);
                    shmdt(shm_ptr);
                    return 1;
                }
            }
        }
    }
    else
    {
        perror("Error en fork()");
        return 1;
    }

    shmdt(shm_ptr);
    return 0;
}

int warehouse_logic_receiver(init_params_client params, connection_context context, shared_data* shm_ptr, int semid)
{
    char* recv = NULL;
    while (1)
    {
        recv = receiver(context, params.connection_params.protocol);
        if (recv == NULL)
        {
            fprintf(stderr, "Error receiving response\n");
            return 1;
        }
        int* next_action = message_receiver(recv, params, context);
        sem_wait(semid, SEM_WRITER);
        shm_ptr->next_action[0] = next_action[0];
        shm_ptr->next_action[1] = next_action[1];
        if (next_action[0] == NOTHING && next_action[1] == WAREHOUSE_LOAD_STOCK)
        {
            shm_ptr->warehouse_load_stock = 1;
            inventory_item* inventory = get_full_inventory();
            memcpy(shm_ptr->items, inventory, sizeof(inventory_item) * get_inventory_size());
        }
        if (next_action[0] == REPLY && next_action[1] == WAREHOUSE_SEND_STOCK_TO_HUB)
        {
            shm_ptr->request_stock_from_hub = 1;
            inventory_item* inventory_to_send = get_full_inventory_to_send();
            memcpy(shm_ptr->items_to_send, inventory_to_send, sizeof(inventory_item) * get_inventory_size());
        }
        sem_signal(semid, SEM_WRITER);
    }
    shmdt(shm_ptr);
    return 0;
}

int hub_logic_sender(init_params_client params, connection_context context, shared_data* shm_ptr, int semid)
{
    int bytes_send;
    pid_t pid = fork();

    if (pid == 0)
    {
        while (1)
        {
            sleep(1);
            sem_wait(semid, SEM_WRITER);
            shm_ptr->timer_tick++;
            sem_signal(semid, SEM_WRITER);
        }
        exit(EXIT_SUCCESS);
    }
    else if (pid > 0)
    {
        connection_context context_hub = context;
        int timer_tick = 0;
        int request_stock = 0;
        int hub_load_stock = 0;
        int hub_delivery = 0;
        int* next_action = malloc(ACTIONS * sizeof(int));
        if (next_action == NULL)
        {
            fprintf(stderr, "Error allocating memory for next_action\n");
            shmdt(shm_ptr);
            return 1;
        }
        inventory_item* items = malloc(sizeof(inventory_item) * get_inventory_size());
        if (items == NULL)
        {
            fprintf(stderr, "Error allocating memory for items\n");
            shmdt(shm_ptr);
            return 1;
        }
        inventory_item* items_to_send = malloc(sizeof(inventory_item) * get_inventory_size());
        if (items_to_send == NULL)
        {
            fprintf(stderr, "Error allocating memory for items_to_send\n");
            free(items);
            shmdt(shm_ptr);
            return 1;
        }
        while (1)
        {
            sem_wait(semid, SEM_WRITER);
            timer_tick = shm_ptr->timer_tick;
            memcpy(items, shm_ptr->items, sizeof(inventory_item) * get_inventory_size());
            memcpy(items_to_send, shm_ptr->items_to_send, sizeof(inventory_item) * get_inventory_size());
            hub_load_stock = shm_ptr->hub_load_stock;
            hub_delivery = shm_ptr->hub_delivery;
            next_action[0] = shm_ptr->next_action[0];
            next_action[1] = shm_ptr->next_action[1];
            if (shm_ptr->timer_tick == 60)
                shm_ptr->timer_tick = 0;
            sem_signal(semid, SEM_WRITER);
            if (compare_inventory(items) && hub_load_stock)
            {
                printf("Inventory updated. Sending to server...\n");
                set_item_quantity(items, ADD);
                log_info("HUB stock updated with ID: %s", get_identifiers()->client_id);
                sem_wait(semid, SEM_WRITER);
                shm_ptr->hub_load_stock = 0;
                sem_signal(semid, SEM_WRITER);
                request_stock = 0;
            };
            if (!verify_inventory() && !request_stock)
            {
                log_info("Low inventory detected, sending request for supply to warehouse with ID: %s",
                         get_identifiers()->client_id);
                bytes_send = message_sender(params, context_hub, HUB_REQUEST_STOCK);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending request for supply\n");
                    shmdt(shm_ptr);
                    return 1;
                }
                request_stock = 1;
            }

            if (hub_delivery)
            {
                set_item_quantity_to_send(items_to_send);
                set_item_quantity(items_to_send, SUBTRACT);
                sem_wait(semid, SEM_WRITER);
                shm_ptr->hub_delivery = 0;
                sem_signal(semid, SEM_WRITER);
            }
            if (timer_tick == 60)
            {
                // printf("Timer activated. Executing warehouse logic...\n");
                // printf("Timer tick: %d\n", timer_tick);
                bytes_send = message_sender(params, context_hub, CLIENT_KEEP_ALIVE);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending keepalive\n");
                    shmdt(shm_ptr);
                    return 1;
                }
                bytes_send = message_sender(params, context_hub, CLIENT_INVENTORY_UPDATE);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending inventory update\n");
                    shmdt(shm_ptr);
                    return 1;
                }
            }

            if (next_action[0] == REPLY)
            {
                sem_wait(semid, SEM_WRITER);
                shm_ptr->next_action[0] = NOTHING;
                sem_signal(semid, SEM_WRITER);
                bytes_send = message_sender(params, context_hub, next_action[1]);
                if (bytes_send < 0)
                {
                    fprintf(stderr, "Error sending inventory update\n");
                    sem_signal(semid, SEM_WRITER);
                    shmdt(shm_ptr);
                    return 1;
                }
            }
        }
    }
    else
    {
        perror("Error en fork()");
        return 1;
    }

    shmdt(shm_ptr);
    return 0;
}

int hub_logic_receiver(init_params_client params, connection_context context, shared_data* shm_ptr, int semid)
{
    char* recv = NULL;
    while (1)
    {
        recv = receiver(context, params.connection_params.protocol);
        if (recv == NULL)
        {
            fprintf(stderr, "Error receiving response\n");
            return 1;
        }
        int* next_action = message_receiver(recv, params, context);
        sem_wait(semid, SEM_WRITER);
        shm_ptr->next_action[0] = next_action[0];
        shm_ptr->next_action[1] = next_action[1];
        if (next_action[0] == NOTHING && next_action[1] == HUB_LOAD_STOCK)
        {
            shm_ptr->hub_load_stock = 1;
            inventory_item* inventory = get_full_inventory();
            memcpy(shm_ptr->items, inventory, sizeof(inventory_item) * get_inventory_size());
        }
        // if (next_action[0] == REPLY && next_action[1] == HUB_DELIVERY)
        // {
        //     shm_ptr->request_stock_from_hub = 1;
        //     inventory_item* inventory_to_send = get_full_inventory_to_send();
        //     memcpy(shm_ptr->items_to_send, inventory_to_send, sizeof(inventory_item) * get_inventory_size());
        // }
        sem_signal(semid, SEM_WRITER);
    }
    shmdt(shm_ptr);
    return 0;
    // Implement hub logic for receiver
    // Recibe un mensaje de broadcast
    // Recibe envios de los almacenes
    // Recibe ordenes de despacho de bienes (aca implementar LASTMILE)
    return 0;
}
