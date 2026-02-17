#define _POSIX_C_SOURCE 200809L
#include "shared_state.h"
#include "json_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define ITEMS_NAME "FOOD", "WATER", "MEDICINE", "TOOLS", "GUNS", "AMMO"

static int shm_fd = -1;
static shared_data_t* shared_data = NULL;
static sem_t* inventory_sem = NULL;
static sem_t* message_available_sem = NULL;

// Store IPC names for cleanup
static char shm_name[64];
static char inventory_sem_name[64];
static char message_sem_name[64];

int ipc_init(const char* client_id)
{
    if (client_id == NULL || strlen(client_id) == 0)
    {
        fprintf(stderr, "[IPC] Invalid client_id provided\n");
        return -1;
    }

    // Create unique IPC names using client_id
    snprintf(shm_name, sizeof(shm_name), "/dhl_client_shm_%s", client_id);
    snprintf(inventory_sem_name, sizeof(inventory_sem_name), "/dhl_inventory_sem_%s", client_id);
    snprintf(message_sem_name, sizeof(message_sem_name), "/dhl_msg_available_sem_%s", client_id);

    // Cleanup any leftover resources from previous runs
    shm_unlink(shm_name);
    sem_unlink(inventory_sem_name);
    sem_unlink(message_sem_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("[IPC] shm_open");
        return -1;
    }

    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1)
    {
        perror("[IPC] ftruncate");
        return -1;
    }

    shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED)
    {
        perror("[IPC] mmap");
        return -1;
    }

    memset(shared_data, 0, sizeof(shared_data_t));

    // Initialize inventory with predefined items (all starting at 0 quantity)
    const char* item_names[QUANTITY_ITEMS] = {ITEMS_NAME};
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].item_id = i + 1; // IDs from 1 to 6
        strncpy(shared_data->inventory_item[i].item_name, item_names[i], ITEM_NAME_SIZE - 1);
        shared_data->inventory_item[i].item_name[ITEM_NAME_SIZE - 1] = '\0';
        shared_data->inventory_item[i].quantity = 0; // Start with 0 quantity
    }
    printf("[IPC] Initialized inventory with %d items (all quantities at 0)\n", QUANTITY_ITEMS);

    inventory_sem = sem_open(inventory_sem_name, O_CREAT | O_EXCL, 0666, 1);
    if (inventory_sem == SEM_FAILED)
    {
        perror("[IPC] sem_open inventory");
        return -1;
    }

    message_available_sem = sem_open(message_sem_name, O_CREAT | O_EXCL, 0666, 0);
    if (message_available_sem == SEM_FAILED)
    {
        perror("[IPC] sem_open message_available");
        return -1;
    }

    printf("[IPC] Initialized successfully for client: %s\n", client_id);
    return 0;
}

void ipc_cleanup(void)
{
    if (shared_data != NULL)
    {
        munmap(shared_data, sizeof(shared_data_t));
        shared_data = NULL;
    }
    if (shm_fd != -1)
    {
        close(shm_fd);
        shm_unlink(shm_name);
        shm_fd = -1;
    }
    if (inventory_sem != NULL)
    {
        sem_close(inventory_sem);
        sem_unlink(inventory_sem_name);
        inventory_sem = NULL;
    }
    if (message_available_sem != NULL)
    {
        sem_close(message_available_sem);
        sem_unlink(message_sem_name);
        message_available_sem = NULL;
    }
    printf("[IPC] Cleaned up\n");
}

int has_pending_messages(void)
{
    int result = 0;
    sem_wait(inventory_sem);
    result = (shared_data->message_count > 0);
    sem_post(inventory_sem);
    return result;
}

int enqueue_pending_message(const message_t* msg)
{
    sem_wait(inventory_sem);
    if (shared_data->message_count >= 10)
    {
        sem_post(inventory_sem);
        fprintf(stderr, "[IPC] Message queue full\n");
        return -1;
    }

    char json_buffer[BUFFER_SIZE];
    if (serialize_message_to_json(msg, json_buffer) != 0)
    {
        sem_post(inventory_sem);
        return -1;
    }

    strncpy(shared_data->pending_messages[shared_data->message_count], json_buffer, BUFFER_SIZE - 1);
    shared_data->message_count++;
    sem_post(inventory_sem);

    sem_post(message_available_sem);

    return 0;
}

int enqueue_pending_message_json(const char* json_message)
{
    sem_wait(inventory_sem);
    if (shared_data->message_count >= 10)
    {
        sem_post(inventory_sem);
        fprintf(stderr, "[IPC] Message queue full\n");
        return -1;
    }

    strncpy(shared_data->pending_messages[shared_data->message_count], json_message, BUFFER_SIZE - 1);
    shared_data->message_count++;
    sem_post(inventory_sem);

    sem_post(message_available_sem);

    return 0;
}

int pop_pending_message(message_t* msg)
{
    sem_wait(inventory_sem);
    if (shared_data->message_count == 0)
    {
        sem_post(inventory_sem);
        return -1;
    }

    char json_buffer[BUFFER_SIZE];
    strncpy(json_buffer, shared_data->pending_messages[0], BUFFER_SIZE - 1);

    // Shift queue
    for (int i = 0; i < shared_data->message_count - 1; i++)
    {
        memcpy(shared_data->pending_messages[i], shared_data->pending_messages[i + 1], BUFFER_SIZE);
    }
    shared_data->message_count--;
    sem_post(inventory_sem);

    return deserialize_message_from_json(json_buffer, msg);
}

int modify_inventory(const inventory_item_t* items, inventory_operation_t operation)
{
    if (items == NULL)
    {
        fprintf(stderr, "[SHARED_STATE] Invalid parameters for modify_inventory\n");
        return -1;
    }

    sem_wait(inventory_sem);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (operation == INVENTORY_ADD)
        {
            shared_data->inventory_item[i].quantity += items[i].quantity;
        }
        else if (operation == INVENTORY_REDUCE)
        {
            int requested_qty = items[i].quantity;
            int available_qty = shared_data->inventory_item[i].quantity;
            int actual_qty = (requested_qty <= available_qty) ? requested_qty : available_qty;

            shared_data->inventory_item[i].quantity -= actual_qty;
        }
    }

    shared_data->inventory_updated = 1;

    sem_post(inventory_sem);
    return 0;
}

int get_inventory_count(int item_id)
{
    int count = -1;
    sem_wait(inventory_sem);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (shared_data->inventory_item[i].item_id == item_id)
        {
            count = shared_data->inventory_item[i].quantity;
            break;
        }
    }

    sem_post(inventory_sem);
    return count;
}

int add_pending_ack(const char* msg_id, const char* msg_type, const char* message_json)
{
    sem_wait(inventory_sem);

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active && strcmp(shared_data->pending_acks[i].msg_id, msg_id) == 0 &&
            strcmp(shared_data->pending_acks[i].msg_type, msg_type) == 0)
        {
            shared_data->pending_acks[i].send_time = time(NULL);
            sem_post(inventory_sem);
            printf("[ACK_TRACK] Updated send_time for existing msg_id: %s, type: %s (retry %d/%d)\n", msg_id, msg_type,
                   shared_data->pending_acks[i].retry_count, MAX_RETRIES);
            return 0;
        }
    }

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (!shared_data->pending_acks[i].active)
        {
            shared_data->pending_acks[i].active = 1;
            shared_data->pending_acks[i].send_time = time(NULL);
            shared_data->pending_acks[i].retry_count = 0;
            strncpy(shared_data->pending_acks[i].msg_id, msg_id, sizeof(shared_data->pending_acks[i].msg_id) - 1);
            strncpy(shared_data->pending_acks[i].msg_type, msg_type, sizeof(shared_data->pending_acks[i].msg_type) - 1);
            strncpy(shared_data->pending_acks[i].message_json, message_json,
                    sizeof(shared_data->pending_acks[i].message_json) - 1);
            sem_post(inventory_sem);
            printf("[ACK_TRACK] Added pending ACK for msg_id: %s, type: %s\n", msg_id, msg_type);
            return 0;
        }
    }

    sem_post(inventory_sem);
    fprintf(stderr, "[ACK_TRACK] No free slots for pending ACK\n");
    return -1;
}

int remove_pending_ack(const char* msg_id)
{
    sem_wait(inventory_sem);

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active && strcmp(shared_data->pending_acks[i].msg_id, msg_id) == 0)
        {
            shared_data->pending_acks[i].active = 0;
            sem_post(inventory_sem);
            printf("[ACK_TRACK] Removed pending ACK for msg_id: %s\n", msg_id);
            return 0;
        }
    }

    sem_post(inventory_sem);
    return -1; // Not found
}

int check_ack_timeouts(void)
{
    time_t now = time(NULL);
    int needs_retransmission = 0;

    sem_wait(inventory_sem);

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active)
        {
            time_t elapsed = now - shared_data->pending_acks[i].send_time;
            if (elapsed >= ACK_TIMEOUT_SECONDS)
            {
                shared_data->pending_acks[i].retry_count++;

                if (shared_data->pending_acks[i].retry_count >= MAX_RETRIES)
                {
                    fprintf(stderr, "[ACK_TRACK] MAX RETRIES (%d) reached for msg_id: %s - Disconnecting\n",
                            MAX_RETRIES, shared_data->pending_acks[i].msg_id);
                    shared_data->ack_timeout_occurred = 1;
                    sem_post(inventory_sem);
                    return -1; // Signal disconnection
                }

                fprintf(stderr, "[ACK_TRACK] TIMEOUT! No ACK for msg_id: %s (retry %d/%d)\n",
                        shared_data->pending_acks[i].msg_id, shared_data->pending_acks[i].retry_count, MAX_RETRIES);

                shared_data->pending_acks[i].send_time = now;
                needs_retransmission = 1;
            }
        }
    }

    sem_post(inventory_sem);
    return needs_retransmission;
}

shared_data_t* get_shared_data(void)
{
    return shared_data;
}

sem_t* get_inventory_sem(void)
{
    return inventory_sem;
}

sem_t* get_message_sem(void)
{
    return message_available_sem;
}
