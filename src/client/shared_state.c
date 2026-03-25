#define _POSIX_C_SOURCE 200809L
#include "shared_state.h"
#include "json_manager.h"
#include "logger.h"
#include "timers.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define ITEMS_NAME "food", "water", "medicine", "tools", "guns", "ammo"
#define IPC_NAME_BUFFER_SIZE 64

static int shm_fd = -1;
static shared_data_t* shared_data = NULL;
static sem_t* inventory_sem = NULL;
static sem_t* message_available_sem = NULL;
static char shm_name[IPC_NAME_BUFFER_SIZE];
static char inventory_sem_name[IPC_NAME_BUFFER_SIZE];
static char message_sem_name[IPC_NAME_BUFFER_SIZE];

static long elapsed_ms(const struct timespec* start, const struct timespec* end)
{
    long sec_ms = (long)(end->tv_sec - start->tv_sec) * 1000L;
    long nsec_ms = (long)(end->tv_nsec - start->tv_nsec) / 1000000L;
    return sec_ms + nsec_ms;
}

int ipc_init(const char* client_id)
{
    if (client_id == NULL || strlen(client_id) == 0)
    {
        LOG_ERROR_MSG("Invalid client_id provided to ipc_init");
        return -1;
    }

    snprintf(shm_name, sizeof(shm_name), "/dhl_client_shm_%s", client_id);
    snprintf(inventory_sem_name, sizeof(inventory_sem_name), "/dhl_inventory_sem_%s", client_id);
    snprintf(message_sem_name, sizeof(message_sem_name), "/dhl_msg_available_sem_%s", client_id);

    shm_unlink(shm_name);
    sem_unlink(inventory_sem_name);
    sem_unlink(message_sem_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        LOG_ERROR_MSG("shm_open failed for %s: %s", shm_name, strerror(errno));
        return -1;
    }

    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1)
    {
        LOG_ERROR_MSG("ftruncate failed for %s: %s", shm_name, strerror(errno));
        return -1;
    }

    shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED)
    {
        LOG_ERROR_MSG("mmap failed for %s: %s", shm_name, strerror(errno));
        return -1;
    }

    memset(shared_data, 0, sizeof(shared_data_t));

    const char* item_names[QUANTITY_ITEMS] = {ITEMS_NAME};
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        shared_data->inventory_item[i].item_id = i + 1;
        strncpy(shared_data->inventory_item[i].item_name, item_names[i], ITEM_NAME_SIZE - 1);
        shared_data->inventory_item[i].item_name[ITEM_NAME_SIZE - 1] = '\0';
        shared_data->inventory_item[i].quantity = 0;
    }
    LOG_INFO_MSG("IPC inventory initialized with %d items", QUANTITY_ITEMS);

    inventory_sem = sem_open(inventory_sem_name, O_CREAT | O_EXCL, 0666, 1);
    if (inventory_sem == SEM_FAILED)
    {
        LOG_ERROR_MSG("sem_open failed for inventory semaphore %s: %s", inventory_sem_name, strerror(errno));
        return -1;
    }

    message_available_sem = sem_open(message_sem_name, O_CREAT | O_EXCL, 0666, 0);
    if (message_available_sem == SEM_FAILED)
    {
        LOG_ERROR_MSG("sem_open failed for message semaphore %s: %s", message_sem_name, strerror(errno));
        return -1;
    }

    LOG_INFO_MSG("IPC initialized successfully for client %s", client_id);
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
    LOG_INFO_MSG("IPC resources cleaned up");
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
    if (shared_data->message_count >= MAX_PENDING_MESSAGES)
    {
        sem_post(inventory_sem);
        LOG_ERROR_MSG("Pending message queue is full");
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
    if (shared_data->message_count >= MAX_PENDING_MESSAGES)
    {
        sem_post(inventory_sem);
        LOG_ERROR_MSG("Pending message queue is full");
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
        LOG_ERROR_MSG("Invalid parameters for modify_inventory");
        return -1;
    }

    sem_wait(inventory_sem);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (items[i].item_id == 0 || items[i].quantity == 0)
            continue;

        for (int j = 0; j < QUANTITY_ITEMS; j++)
        {
            if (shared_data->inventory_item[j].item_id != items[i].item_id)
                continue;

            if (operation == INVENTORY_ADD)
            {
                shared_data->inventory_item[j].quantity += items[i].quantity;
            }
            else if (operation == INVENTORY_REDUCE)
            {
                int requested_qty = items[i].quantity;
                int available_qty = shared_data->inventory_item[j].quantity;
                int actual_qty = (requested_qty <= available_qty) ? requested_qty : available_qty;

                shared_data->inventory_item[j].quantity -= actual_qty;
            }
            break;
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

int get_inventory_snapshot(inventory_item_t* out_items)
{
    if (out_items == NULL)
    {
        return -1;
    }

    sem_wait(inventory_sem);
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        out_items[i].item_id = shared_data->inventory_item[i].item_id;
        strncpy(out_items[i].item_name, shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE - 1);
        out_items[i].item_name[ITEM_NAME_SIZE - 1] = '\0';
        out_items[i].quantity = shared_data->inventory_item[i].quantity;
    }
    sem_post(inventory_sem);

    return 0;
}

int count_items_above_threshold(int threshold)
{
    int count = 0;

    sem_wait(inventory_sem);
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (shared_data->inventory_item[i].quantity >= threshold)
        {
            count++;
        }
    }
    sem_post(inventory_sem);

    return count;
}

int get_low_stock_report(int low_threshold, int critical_threshold, int max_stock, inventory_item_t* out_low_items,
                         int* out_item_indices, int* out_critical_count)
{
    if (out_low_items == NULL || out_item_indices == NULL || out_critical_count == NULL)
    {
        return -1;
    }

    int low_count = 0;
    int critical_count = 0;

    sem_wait(inventory_sem);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        int current_quantity = shared_data->inventory_item[i].quantity;

        if (current_quantity < low_threshold && !shared_data->pending_stock_request[i])
        {
            out_low_items[low_count].item_id = shared_data->inventory_item[i].item_id;
            strncpy(out_low_items[low_count].item_name, shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE - 1);
            out_low_items[low_count].item_name[ITEM_NAME_SIZE - 1] = '\0';

            int needed = max_stock - current_quantity;
            out_low_items[low_count].quantity = (needed > 0) ? needed : 0;

            out_item_indices[low_count] = i;
            low_count++;
        }

        if (current_quantity < critical_threshold)
        {
            critical_count++;
        }
    }

    sem_post(inventory_sem);

    *out_critical_count = critical_count;
    return low_count;
}

void log_inventory_snapshot(const char* context)
{
    inventory_item_t snapshot[QUANTITY_ITEMS];
    if (get_inventory_snapshot(snapshot) != 0)
    {
        LOG_DEBUG_MSG("[%s] inventory snapshot unavailable", context ? context : "?");
        return;
    }

    LOG_DEBUG_MSG("[%s] Current inventory:", context ? context : "?");
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        LOG_DEBUG_MSG("  item_id=%d  %-10s  qty=%d", snapshot[i].item_id, snapshot[i].item_name, snapshot[i].quantity);
    }
}

int build_full_request_payload(const inventory_item_t* low_items, int low_count, inventory_item_t* out_full_items)
{
    if (out_full_items == NULL || low_items == NULL || low_count < 0)
    {
        return -1;
    }

    if (get_inventory_snapshot(out_full_items) != 0)
    {
        return -1;
    }

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        out_full_items[i].quantity = 0;
    }

    for (int i = 0; i < low_count; i++)
    {
        int low_item_id = low_items[i].item_id;
        for (int j = 0; j < QUANTITY_ITEMS; j++)
        {
            if (out_full_items[j].item_id == low_item_id)
            {
                out_full_items[j].quantity = low_items[i].quantity;
                break;
            }
        }
    }

    return 0;
}

void mark_pending_stock_requests(const int* item_indices, int count)
{
    if (item_indices == NULL || count <= 0)
    {
        return;
    }

    sem_wait(inventory_sem);
    for (int i = 0; i < count; i++)
    {
        int idx = item_indices[i];
        if (idx < 0 || idx >= QUANTITY_ITEMS)
        {
            continue;
        }
        shared_data->pending_stock_request[idx] = 1;
    }
    sem_post(inventory_sem);
}

void clear_pending_stock_requests(const inventory_item_t* items, int count)
{
    if (items == NULL || count <= 0)
    {
        return;
    }

    sem_wait(inventory_sem);
    for (int i = 0; i < count; i++)
    {
        if (items[i].quantity <= 0)
        {
            continue;
        }

        int item_index = items[i].item_id - 1;
        if (item_index < 0 || item_index >= QUANTITY_ITEMS)
        {
            continue;
        }

        shared_data->pending_stock_request[item_index] = 0;
    }
    sem_post(inventory_sem);
}

int collect_retryable_messages(char out_messages[][BUFFER_SIZE], int max_count)
{
    if (out_messages == NULL || max_count <= 0)
    {
        return 0;
    }

    int collected = 0;

    sem_wait(inventory_sem);
    for (int i = 0; i < MAX_PENDING_ACKS && collected < max_count; i++)
    {
        if (shared_data->pending_acks[i].active && shared_data->pending_acks[i].retry_count > 0)
        {
            strncpy(out_messages[collected], shared_data->pending_acks[i].message_json, BUFFER_SIZE - 1);
            out_messages[collected][BUFFER_SIZE - 1] = '\0';
            collected++;
        }
    }
    sem_post(inventory_sem);

    return collected;
}

int wait_for_message(int timeout_seconds)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
        return -1;
    }

    ts.tv_sec += timeout_seconds;

    while (sem_timedwait(message_available_sem, &ts) == -1)
    {
        if (errno == ETIMEDOUT || errno == EINTR)
        {
            return 1;
        }

        return -1;
    }

    return 0;
}

void wake_sender_thread(void)
{
    sem_post(message_available_sem);
}

int add_pending_ack(const char* msg_id, const char* msg_type, const char* message_json)
{
    const timer_config_t* cfg = get_timer_config();
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return -1;
    }

    sem_wait(inventory_sem);

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active && strcmp(shared_data->pending_acks[i].msg_id, msg_id) == 0 &&
            strcmp(shared_data->pending_acks[i].msg_type, msg_type) == 0)
        {
            shared_data->pending_acks[i].send_time = now;
            sem_post(inventory_sem);
            LOG_DEBUG_MSG("Updated pending ACK timestamp for %s (%s), retry %d/%d", msg_id, msg_type,
                          shared_data->pending_acks[i].retry_count, cfg->max_retries);
            return 0;
        }
    }

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (!shared_data->pending_acks[i].active)
        {
            shared_data->pending_acks[i].active = 1;
            shared_data->pending_acks[i].send_time = now;
            shared_data->pending_acks[i].retry_count = 0;
            strncpy(shared_data->pending_acks[i].msg_id, msg_id, sizeof(shared_data->pending_acks[i].msg_id) - 1);
            strncpy(shared_data->pending_acks[i].msg_type, msg_type, sizeof(shared_data->pending_acks[i].msg_type) - 1);
            strncpy(shared_data->pending_acks[i].message_json, message_json,
                    sizeof(shared_data->pending_acks[i].message_json) - 1);
            sem_post(inventory_sem);
            LOG_DEBUG_MSG("Added pending ACK for %s (%s)", msg_id, msg_type);
            return 0;
        }
    }

    sem_post(inventory_sem);
    LOG_ERROR_MSG("No free slots available for pending ACK tracking");
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
            LOG_DEBUG_MSG("Removed pending ACK for %s", msg_id);
            return 0;
        }
    }

    sem_post(inventory_sem);
    return -1;
}

int check_ack_timeouts(void)
{
    const timer_config_t* cfg = get_timer_config();
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return -1;
    }

    int needs_retransmission = 0;

    sem_wait(inventory_sem);

    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (shared_data->pending_acks[i].active)
        {
            long elapsed = elapsed_ms(&shared_data->pending_acks[i].send_time, &now);
            if (elapsed >= cfg->ack_timeout_ms)
            {
                shared_data->pending_acks[i].retry_count++;

                if (shared_data->pending_acks[i].retry_count >= cfg->max_retries)
                {
                    LOG_ERROR_MSG("Max retries reached for pending ACK %s (%d/%d)", shared_data->pending_acks[i].msg_id,
                                  shared_data->pending_acks[i].retry_count, cfg->max_retries);
                    shared_data->ack_timeout_occurred = 1;
                    sem_post(inventory_sem);
                    return -1;
                }

                LOG_WARNING_MSG("ACK timeout for %s (retry %d/%d, timeout=%dms)", shared_data->pending_acks[i].msg_id,
                                shared_data->pending_acks[i].retry_count, cfg->max_retries, cfg->ack_timeout_ms);

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
