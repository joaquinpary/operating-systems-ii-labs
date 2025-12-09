#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "json_manager.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int shm_fd = -1;
static shared_data_t* shared_data = NULL;
static sem_t* inventory_sem = NULL;
static sem_t* message_available_sem = NULL;

int ipc_init(void)
{
    shm_unlink("/dhl_client_shm");
    sem_unlink("/dhl_inventory_sem");
    sem_unlink("/dhl_msg_available_sem");

    shm_fd = shm_open("/dhl_client_shm", O_CREAT | O_EXCL | O_RDWR, 0666);
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

    inventory_sem = sem_open("/dhl_inventory_sem", O_CREAT | O_EXCL, 0666, 1);
    if (inventory_sem == SEM_FAILED)
    {
        perror("[IPC] sem_open inventory");
        return -1;
    }

    message_available_sem = sem_open("/dhl_msg_available_sem", O_CREAT | O_EXCL, 0666, 0);
    if (message_available_sem == SEM_FAILED)
    {
        perror("[IPC] sem_open message_available");
        return -1;
    }

    printf("[IPC] Initialized successfully\n");
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
        shm_unlink("/dhl_client_shm");
        shm_fd = -1;
    }
    if (inventory_sem != NULL)
    {
        sem_close(inventory_sem);
        sem_unlink("/dhl_inventory_sem");
        inventory_sem = NULL;
    }
    if (message_available_sem != NULL)
    {
        sem_close(message_available_sem);
        sem_unlink("/dhl_msg_available_sem");
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

// Estas funciones son placeholders
int update_inventory(int item_id, int quantity)
{
    sem_wait(inventory_sem);
    if (item_id >= 0 && item_id < 100)
    {
        shared_data->inventory[item_id] += quantity;
        printf("[INVENTORY] Item %d updated: %d units\n", item_id, shared_data->inventory[item_id]);
    }
    sem_post(inventory_sem);
    return 0;
}

int get_inventory_count(int item_id)
{
    int count = 0;
    sem_wait(inventory_sem);
    if (item_id >= 0 && item_id < 100)
    {
        count = shared_data->inventory[item_id];
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
