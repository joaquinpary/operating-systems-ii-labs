#define _POSIX_C_SOURCE 200809L
#include "logic.h"
#include "connection.h"
#include "json_manager.h"
#include "logger.h"
#include "shared_state.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Configuration constants for business logic
#define INVENTORY_UPDATE_INTERVAL_SEC 60 // Periodic inventory reporting
#define CONSUME_STOCK_MIN_SEC 2          // Minimum seconds between stock consumption
#define CONSUME_STOCK_MAX_SEC 10         // Maximum seconds between stock consumption
#define CONSUME_MIN_AMOUNT 1             // Minimum units to consume per item
#define CONSUME_MAX_AMOUNT 20            // Maximum units to consume per item
#define LOW_STOCK_THRESHOLD 20           // Threshold to trigger stock request
#define CRITICAL_STOCK_THRESHOLD 5       // Threshold to disable consumption
#define MAX_STOCK_PER_ITEM 100           // Maximum inventory capacity per item
#define REORDER_QUANTITY 50              // Amount requested when low stock

static client_context* logic_ctx = NULL;

/**
 * @brief Generate random interval for stock consumption
 * @return Random seconds between CONSUME_STOCK_MIN_SEC and CONSUME_STOCK_MAX_SEC
 */
static int get_random_consume_interval(void)
{
    static int seeded = 0;
    if (!seeded)
    {
        srand(time(NULL) ^ getpid());
        seeded = 1;
    }
    return CONSUME_STOCK_MIN_SEC + (rand() % (CONSUME_STOCK_MAX_SEC - CONSUME_STOCK_MIN_SEC + 1));
}

/**
 * @brief Generate random consumption amount for an item
 * @return Random units between CONSUME_MIN_AMOUNT and CONSUME_MAX_AMOUNT
 */
static int get_random_consume_amount(void)
{
    return CONSUME_MIN_AMOUNT + (rand() % (CONSUME_MAX_AMOUNT - CONSUME_MIN_AMOUNT + 1));
}

static void* ack_timeout_checker_thread(void* arg)
{
    shared_data_t* shared_data = get_shared_data();
    sem_t* message_available_sem = get_message_sem();

    LOG_INFO_MSG("ACK checker thread started");

    while (!shared_data->should_exit)
    {
        int result = check_ack_timeouts();

        if (result < 0)
        {
            LOG_ERROR_MSG("Max retries exceeded, triggering shutdown");
            shared_data->should_exit = 1;
            sem_post(message_available_sem);
            break;
        }
        else if (result > 0)
        {
            char messages_to_retry[MAX_PENDING_ACKS][BUFFER_SIZE];
            int retry_count = 0;

            sem_t* inventory_sem = get_inventory_sem();
            sem_wait(inventory_sem);
            for (int i = 0; i < MAX_PENDING_ACKS; i++)
            {
                if (shared_data->pending_acks[i].active && shared_data->pending_acks[i].retry_count > 0)
                {
                    LOG_DEBUG_MSG("Re-enqueueing msg_id: %s (attempt %d)", shared_data->pending_acks[i].msg_id,
                                  shared_data->pending_acks[i].retry_count);

                    strncpy(messages_to_retry[retry_count], shared_data->pending_acks[i].message_json, BUFFER_SIZE - 1);
                    retry_count++;
                }
            }
            sem_post(inventory_sem);

            for (int i = 0; i < retry_count; i++)
            {
                if (enqueue_pending_message_json(messages_to_retry[i]) != 0)
                {
                    LOG_ERROR_MSG("Failed to re-enqueue message");
                }
            }
        }

        sleep(1);
    }

    LOG_INFO_MSG("ACK checker thread exiting");
    return NULL;
}

static int handle_server_message(const message_t* msg)
{
    shared_data_t* shared_data = get_shared_data();

    printf("[RECEIVER] Got message type: %s\n", msg->msg_type);

    if (strstr(msg->msg_type, "ACK") != NULL)
    {
        remove_pending_ack(msg->payload.acknowledgment.ack_for_timestamp);
        return 0;
    }

    if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE) == 0 ||
        strcmp(msg->msg_type, SERVER_TO_HUB__AUTH_RESPONSE) == 0)
    {
        message_t ack_msg;

        if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                          msg->timestamp, 200) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK message");
            fprintf(stderr, "[RECEIVER] Failed to create ACK message\n");
            return -1;
        }

        if (enqueue_pending_message(&ack_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue ACK");
            fprintf(stderr, "[RECEIVER] Failed to enqueue ACK\n");
            return -1;
        }

        LOG_DEBUG_MSG("ACK enqueued for message type: %s", msg->msg_type);
    }
    else if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__INVENTORY_UPDATE) == 0 ||
             strcmp(msg->msg_type, SERVER_TO_HUB__INVENTORY_UPDATE) == 0)
    {
        if (modify_inventory(msg->payload.inventory_update.items, INVENTORY_ADD) != 0)
        {
            LOG_ERROR_MSG("Failed to update inventory from server message");
            fprintf(stderr, "[RECEIVER] Failed to update inventory from server message\n");
            return -1;
        }

        message_t ack_msg;
        if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                          msg->timestamp, 200) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK message for INVENTORY_UPDATE");
            fprintf(stderr, "[RECEIVER] Failed to create ACK message for INVENTORY_UPDATE\n");
            return -1;
        }

        if (enqueue_pending_message(&ack_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue ACK for INVENTORY_UPDATE");
            fprintf(stderr, "[RECEIVER] Failed to enqueue ACK for INVENTORY_UPDATE\n");
            return -1;
        }

        LOG_DEBUG_MSG("ACK enqueued for INVENTORY_UPDATE message");
    }
    else if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB) == 0)
    {
        LOG_INFO_MSG("Received dispatch order to HUB: %s", msg->target_id);

        // Send ACK to server (early return pattern)
        message_t ack_msg;
        if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                          msg->timestamp, 200) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK message for dispatch order");
            return -1;
        }

        if (enqueue_pending_message(&ack_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue ACK for dispatch order");
            return -1;
        }
        LOG_DEBUG_MSG("ACK sent for dispatch order");

        // Reduce inventory
        const payload_order_stock* order = &msg->payload.order_stock;
        if (modify_inventory(order->items, INVENTORY_REDUCE) != 0)
        {
            LOG_ERROR_MSG("Failed to reduce inventory for dispatch order");
            return -1;
        }

        // Send shipment notice
        message_t shipment_msg;
        if (create_items_message(&shipment_msg, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, shared_data->client_id,
                                 msg->target_id, // Forward to target HUB
                                 order->items, QUANTITY_ITEMS, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create shipment notice message");
            return -1;
        }

        if (enqueue_pending_message(&shipment_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue shipment notice");
            return -1;
        }
        LOG_INFO_MSG("Shipment notice sent, dispatching to HUB %s", msg->target_id);
    }
    else if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE) == 0 ||
             strcmp(msg->msg_type, SERVER_TO_HUB__INCOMING_STOCK_NOTICE) == 0)
    {
        LOG_INFO_MSG("Received stock delivery from server");

        // Send ACK to server (guard clause pattern)
        message_t ack_msg;
        if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                          msg->timestamp, 200) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK message for stock delivery");
            return -1;
        }

        if (enqueue_pending_message(&ack_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue ACK for stock delivery");
            return -1;
        }
        LOG_DEBUG_MSG("ACK sent for stock delivery");

        // Add inventory (stock from server)
        const payload_restock_notice* restock = &msg->payload.restock_notice;
        if (modify_inventory(restock->items, INVENTORY_ADD) != 0)
        {
            LOG_ERROR_MSG("Failed to add inventory for stock delivery");
            return -1;
        }

        // Send receipt confirmation (role-specific message type)
        message_t receipt_msg;
        const char* receipt_type = (strcmp(shared_data->client_role, HUB) == 0)
                                       ? HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION
                                       : WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION;

        if (create_items_message(&receipt_msg, receipt_type, shared_data->client_id, SERVER, restock->items,
                                 QUANTITY_ITEMS, msg->timestamp) != 0)
        {
            LOG_ERROR_MSG("Failed to create receipt confirmation message");
            return -1;
        }

        if (enqueue_pending_message(&receipt_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue receipt confirmation");
            return -1;
        }
        LOG_INFO_MSG("Stock delivery processed and receipt confirmation sent");
    }
    return 0;
}

static void* receiver_thread(void* arg)
{
    client_context* ctx = (client_context*)arg;
    char buffer[BUFFER_SIZE];
    shared_data_t* shared_data = get_shared_data();

    printf("[RECEIVER] Thread started\n");

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        LOG_ERROR_MSG("Failed to set socket receive timeout: %s", strerror(errno));
    }

    while (!shared_data->should_exit)
    {
        int bytes = client_receive(ctx, buffer, BUFFER_SIZE);
        if (bytes > 0)
        {
            message_t msg;
            if (deserialize_message_from_json(buffer, &msg) == 0)
            {
                handle_server_message(&msg);
            }
            else
            {
                LOG_ERROR_MSG("Failed to deserialize received message");
            }
        }
        else if (bytes == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            LOG_ERROR_MSG("Receive error: %s", strerror(errno));
            break;
        }
        else if (bytes == -2)
        {
            LOG_WARNING_MSG("Connection closed by server");
            shared_data->should_exit = 1;
            break;
        }
    }

    LOG_INFO_MSG("Receiver thread exiting");
    return NULL;
}

static void* sender_thread(void* arg)
{
    client_context* ctx = (client_context*)arg;
    message_t msg;
    shared_data_t* shared_data = get_shared_data();
    sem_t* message_available_sem = get_message_sem();

    LOG_INFO_MSG("Sender thread started");

    while (!shared_data->should_exit)
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int sem_result = sem_timedwait(message_available_sem, &ts);

        if (sem_result == 0)
        {
            if (pop_pending_message(&msg) == 0)
            {
                char json_buffer[BUFFER_SIZE];
                if (serialize_message_to_json(&msg, json_buffer) == 0)
                {
                    if (client_send(ctx, json_buffer) == 0)
                    {
                        printf("[SENDER] Message sent: type=%s, timestamp=%s\n", msg.msg_type, msg.timestamp);
                        LOG_DEBUG_MSG("Message sent: type=%s, timestamp=%s", msg.msg_type, msg.timestamp);

                        if (strstr(msg.msg_type, "ACK") == NULL && strstr(msg.msg_type, "AUTH_REQUEST") == NULL &&
                            strstr(msg.msg_type, "KEEPALIVE") == NULL)
                        {
                            add_pending_ack(msg.timestamp, msg.msg_type, json_buffer);
                        }
                    }
                    else
                    {
                        LOG_ERROR_MSG("Failed to send message");
                    }
                }
            }
        }
        else if (errno == ETIMEDOUT)
        {
            continue;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            LOG_ERROR_MSG("sem_timedwait error: %s", strerror(errno));
            break;
        }
    }

    LOG_INFO_MSG("Sender thread exiting");
    return NULL;
}

// ==================== BUSINESS LOGIC FUNCTIONS ====================

/**
 * @brief Send periodic inventory update to server
 *
 * Reads current inventory state from shared memory and sends
 * an INVENTORY_UPDATE message to the server.
 */
static void do_inventory_update(void)
{
    shared_data_t* shared_data = get_shared_data();

    message_t msg;

    if (create_keepalive_message(&msg, shared_data->client_role, shared_data->client_id, ALIVE) != 0)
    {
        LOG_ERROR_MSG("Failed to create keepalive message");
        return;
    }

    if (enqueue_pending_message(&msg) != 0)
    {
        LOG_ERROR_MSG("Failed to enqueue keepalive message");
        return;
    }

    LOG_DEBUG_MSG("Keepalive message enqueued");

    inventory_item_t items[QUANTITY_ITEMS];

    sem_wait(get_inventory_sem());

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        items[i].item_id = shared_data->inventory_item[i].item_id;
        strncpy(items[i].item_name, shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE - 1);
        items[i].quantity = shared_data->inventory_item[i].quantity;
    }

    printf("ITEMS:\n");
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        printf(" - %s: %d units\n", items[i].item_name, items[i].quantity);
    }

    sem_post(get_inventory_sem());

    // Determine correct message type based on client role
    const char* msg_type = (strcmp(shared_data->client_role, HUB) == 0) ? HUB_TO_SERVER__INVENTORY_UPDATE
                                                                        : WAREHOUSE_TO_SERVER__INVENTORY_UPDATE;

    if (create_items_message(&msg, msg_type, shared_data->client_id, SERVER, items, QUANTITY_ITEMS, NULL) != 0)
    {
        LOG_ERROR_MSG("Failed to create inventory update message");
        return;
    }

    if (enqueue_pending_message(&msg) != 0)
    {
        LOG_ERROR_MSG("Failed to enqueue inventory update");
        return;
    }

    LOG_DEBUG_MSG("Inventory update enqueued (%d items)", QUANTITY_ITEMS);
}

/**
 * @brief Simulate stock consumption
 *
 * Reduces inventory quantities to simulate deliveries (HUB) or
 * dispatches (WAREHOUSE). Consumes a random amount (1-20) per item.
 */
static void do_consume_stock(void)
{
    shared_data_t* shared_data = get_shared_data();

    // Prepare items to reduce with random amounts
    inventory_item_t items_to_reduce[QUANTITY_ITEMS];
    int items_with_stock = 0;
    int total_to_consume = 0;

    sem_wait(get_inventory_sem());

    // Build list of items to consume with random amounts
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        items_to_reduce[i].item_id = shared_data->inventory_item[i].item_id;
        strncpy(items_to_reduce[i].item_name, shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE - 1);
        items_to_reduce[i].item_name[ITEM_NAME_SIZE - 1] = '\0';

        if (shared_data->inventory_item[i].quantity > 0)
        {
            items_to_reduce[i].quantity = get_random_consume_amount();
            total_to_consume += items_to_reduce[i].quantity;
            items_with_stock++;
        }
        else
        {
            items_to_reduce[i].quantity = 0;
        }
    }

    sem_post(get_inventory_sem());

    // If no items have stock, return early
    if (items_with_stock == 0)
    {
        LOG_WARNING_MSG("No stock available to consume");
        return;
    }

    // Reduce inventory (server manages stock levels)
    if (modify_inventory(items_to_reduce, INVENTORY_REDUCE) != 0)
    {
        LOG_ERROR_MSG("Failed to reduce inventory");
        return;
    }

    LOG_INFO_MSG("Stock consumption completed: %d items affected, ~%d total units consumed", items_with_stock,
                 total_to_consume);
}

/**
 * @brief Check for low stock levels and request replenishment
 *
 * Scans inventory for items below threshold (20 units) and sends a STOCK_REQUEST
 * message to the server if low stock is detected. Requests up to MAX_STOCK_PER_ITEM.
 *
 * @return 1 if all items are critically low (need to pause consumption), 0 otherwise
 */
static int do_check_low_stock(void)
{
    shared_data_t* shared_data = get_shared_data();
    inventory_item_t low_items[QUANTITY_ITEMS];
    int low_count = 0;
    int critically_low_count = 0;

    sem_wait(get_inventory_sem());

    // Check each item against threshold
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        if (shared_data->inventory_item[i].quantity < LOW_STOCK_THRESHOLD)
        {
            // Add to low stock list
            low_items[low_count].item_id = shared_data->inventory_item[i].item_id;
            strncpy(low_items[low_count].item_name, shared_data->inventory_item[i].item_name, ITEM_NAME_SIZE - 1);
            low_items[low_count].item_name[ITEM_NAME_SIZE - 1] = '\0';

            // Request enough to reach MAX_STOCK_PER_ITEM
            int needed = MAX_STOCK_PER_ITEM - shared_data->inventory_item[i].quantity;
            low_items[low_count].quantity = needed;

            LOG_DEBUG_MSG("Low stock detected: %s (ID: %d) has %d units, requesting %d units",
                          shared_data->inventory_item[i].item_name, shared_data->inventory_item[i].item_id,
                          shared_data->inventory_item[i].quantity, needed);

            low_count++;
        }

        if (shared_data->inventory_item[i].quantity < CRITICAL_STOCK_THRESHOLD)
        {
            critically_low_count++;
        }
    }

    sem_post(get_inventory_sem());

    if (low_count > 0)
    {
        LOG_INFO_MSG("Low stock detected for %d items, requesting replenishment", low_count);

        message_t msg;

        const char* msg_type = (strcmp(shared_data->client_role, HUB) == 0) ? HUB_TO_SERVER__STOCK_REQUEST
                                                                            : WAREHOUSE_TO_SERVER__REPLENISH_REQUEST;

        if (create_items_message(&msg, msg_type, shared_data->client_id, SERVER, low_items, low_count, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create stock request message");
            return critically_low_count == QUANTITY_ITEMS ? 1 : 0;
        }

        if (enqueue_pending_message(&msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue stock request");
            return critically_low_count == QUANTITY_ITEMS ? 1 : 0;
        }

        LOG_INFO_MSG("Stock request enqueued for %d items", low_count);
    }

    // If ALL items are critically low, signal to pause consumption
    if (critically_low_count == QUANTITY_ITEMS)
    {
        LOG_WARNING_MSG("All inventory critically low, consumption will be paused");
        return 1;
    }

    return 0;
}

// ==================== CHILD LOGIC PROCESS ====================

static int child_logic_process(void)
{
    shared_data_t* shared_data = get_shared_data();

    LOG_INFO_MSG("Business logic process started (PID: %d)", getpid());

    // Create timer file descriptors
    int inventory_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (inventory_timer_fd == -1)
    {
        LOG_ERROR_MSG("Failed to create inventory timer: %s", strerror(errno));
        return -1;
    }

    int consume_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (consume_timer_fd == -1)
    {
        LOG_ERROR_MSG("Failed to create consume timer: %s", strerror(errno));
        close(inventory_timer_fd);
        return -1;
    }

    // Configure inventory update timer (60 seconds, repeating)
    struct itimerspec inventory_spec = {.it_interval = {.tv_sec = INVENTORY_UPDATE_INTERVAL_SEC, .tv_nsec = 0},
                                        .it_value = {.tv_sec = INVENTORY_UPDATE_INTERVAL_SEC, .tv_nsec = 0}};
    if (timerfd_settime(inventory_timer_fd, 0, &inventory_spec, NULL) == -1)
    {
        LOG_ERROR_MSG("Failed to set inventory timer: %s", strerror(errno));
        close(inventory_timer_fd);
        close(consume_timer_fd);
        return -1;
    }

    // Configure stock consumption timer (random interval between 2-10 seconds, one-shot)
    int consume_interval = get_random_consume_interval();
    struct itimerspec consume_spec = {.it_interval = {.tv_sec = 0, .tv_nsec = 0}, // One-shot timer (no repeat)
                                      .it_value = {.tv_sec = consume_interval, .tv_nsec = 0}};
    if (timerfd_settime(consume_timer_fd, 0, &consume_spec, NULL) == -1)
    {
        LOG_ERROR_MSG("Failed to set consume timer: %s", strerror(errno));
        close(inventory_timer_fd);
        close(consume_timer_fd);
        return -1;
    }

    LOG_INFO_MSG("Timers configured: inventory=%ds, consume=random(%d-%ds), first=%ds", INVENTORY_UPDATE_INTERVAL_SEC,
                 CONSUME_STOCK_MIN_SEC, CONSUME_STOCK_MAX_SEC, consume_interval);

    // Event loop with select()
    int maxfd = (inventory_timer_fd > consume_timer_fd) ? inventory_timer_fd : consume_timer_fd;
    fd_set readfds;
    int consume_timer_active = 1; // Track if consume timer is active

    while (!shared_data->should_exit)
    {
        if (shared_data->inventory_updated && !consume_timer_active)
        {
            shared_data->inventory_updated = 0; // Reset flag

            sem_wait(get_inventory_sem());
            int items_with_stock = 0;
            for (int i = 0; i < QUANTITY_ITEMS; i++)
            {
                if (shared_data->inventory_item[i].quantity >= 5)
                {
                    items_with_stock++;
                }
            }
            sem_post(get_inventory_sem());

            if (items_with_stock > 0)
            {
                int next_interval = get_random_consume_interval();
                struct itimerspec resume_spec = {.it_interval = {.tv_sec = 0, .tv_nsec = 0},
                                                 .it_value = {.tv_sec = next_interval, .tv_nsec = 0}};

                if (timerfd_settime(consume_timer_fd, 0, &resume_spec, NULL) == -1)
                {
                    LOG_ERROR_MSG("Failed to reactivate consume timer: %s", strerror(errno));
                }
                else
                {
                    consume_timer_active = 1;
                    LOG_INFO_MSG("Consume timer reactivated after inventory update (next in %ds)", next_interval);
                }
            }
        }

        FD_ZERO(&readfds);
        FD_SET(inventory_timer_fd, &readfds);
        FD_SET(consume_timer_fd, &readfds);

        // Use timeout to check should_exit periodically
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

        if (ret == -1)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal, check should_exit and continue
                continue;
            }
            LOG_ERROR_MSG("select() error: %s", strerror(errno));
            break;
        }
        else if (ret > 0)
        {
            uint64_t expirations;

            // Check inventory update timer
            if (FD_ISSET(inventory_timer_fd, &readfds))
            {
                if (read(inventory_timer_fd, &expirations, sizeof(expirations)) > 0)
                {
                    LOG_DEBUG_MSG("Inventory timer expired (%lu times)", expirations);
                    do_inventory_update();
                }
            }

            // Check stock consumption timer (only HUBs consume stock)
            if (FD_ISSET(consume_timer_fd, &readfds))
            {
                if (read(consume_timer_fd, &expirations, sizeof(expirations)) > 0)
                {
                    LOG_DEBUG_MSG("Consume timer expired (%lu times)", expirations);

                    // Only HUBs consume stock (simulating deliveries to customers)
                    // WAREHOUSEs don't consume, they only dispatch to HUBs
                    if (strcmp(shared_data->client_role, HUB) == 0)
                    {
                        do_consume_stock();

                        // Check if stock is critically low
                        int all_low = do_check_low_stock();

                        if (all_low)
                        {
                            // Disable consume timer until stock arrives
                            struct itimerspec disable_spec = {.it_interval = {.tv_sec = 0, .tv_nsec = 0},
                                                              .it_value = {.tv_sec = 0, .tv_nsec = 0}};

                            if (timerfd_settime(consume_timer_fd, 0, &disable_spec, NULL) == -1)
                            {
                                LOG_ERROR_MSG("Failed to disable consume timer: %s", strerror(errno));
                            }
                            else
                            {
                                consume_timer_active = 0;
                                LOG_INFO_MSG("Consume timer disabled due to critically low stock");
                            }
                        }
                        else
                        {
                            // Reconfigure timer with new random interval (one-shot)
                            int next_interval = get_random_consume_interval();
                            struct itimerspec new_consume_spec = {.it_interval = {.tv_sec = 0, .tv_nsec = 0},
                                                                  .it_value = {.tv_sec = next_interval, .tv_nsec = 0}};

                            if (timerfd_settime(consume_timer_fd, 0, &new_consume_spec, NULL) == -1)
                            {
                                LOG_ERROR_MSG("Failed to reschedule consume timer: %s", strerror(errno));
                            }
                            else
                            {
                                LOG_DEBUG_MSG("Next consume in %d seconds", next_interval);
                            }
                        }
                    }
                    else
                    {
                        LOG_DEBUG_MSG("Consume timer fired for WAREHOUSE (ignored - warehouses don't consume stock)");
                    }
                }
            }
        }
    }

    close(inventory_timer_fd);
    close(consume_timer_fd);

    LOG_INFO_MSG("Business logic process exiting");
    return 0;
}

// ==================== MAIN LOGIC ENTRY ====================

int logic_init(client_context* ctx, const char* client_role, const char* client_id)
{
    logic_ctx = ctx;

    LOG_INFO_MSG("Initializing client logic for %s/%s", client_role, client_id);

    if (ipc_init(client_id) != 0)
    {
        LOG_ERROR_MSG("Failed to initialize IPC");
        return -1;
    }

    // Get shared data and store client identity
    shared_data_t* shared_data = get_shared_data();
    strncpy(shared_data->client_role, client_role, sizeof(shared_data->client_role) - 1);
    strncpy(shared_data->client_id, client_id, sizeof(shared_data->client_id) - 1);
    LOG_INFO_MSG("Client identity set: %s / %s", shared_data->client_role, shared_data->client_id);

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR_MSG("Fork failed: %s", strerror(errno));
        ipc_cleanup();
        return -1;
    }
    else if (pid == 0)
    {
        // ============ CHILD PROCESS ============
        int ret = child_logic_process();
        ipc_cleanup();
        exit(ret);
    }
    else
    {
        // ============ PARENT PROCESS ============
        LOG_INFO_MSG("Network handler started (PID: %d, Child PID: %d)", getpid(), pid);

        pthread_t receiver, sender, ack_checker;

        // Create receiver thread
        if (pthread_create(&receiver, NULL, receiver_thread, ctx) != 0)
        {
            LOG_ERROR_MSG("Failed to create receiver thread");
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        // Create sender thread
        if (pthread_create(&sender, NULL, sender_thread, ctx) != 0)
        {
            LOG_ERROR_MSG("Failed to create sender thread");
            shared_data->should_exit = 1;
            pthread_join(receiver, NULL);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        // Create ACK checker thread
        if (pthread_create(&ack_checker, NULL, ack_timeout_checker_thread, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create ACK checker thread");
            shared_data->should_exit = 1;
            sem_post(get_message_sem());
            pthread_join(receiver, NULL);
            pthread_join(sender, NULL);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            ipc_cleanup();
            return -1;
        }

        // Wait for child process
        int status;
        waitpid(pid, &status, 0);
        LOG_INFO_MSG("Child process exited with status %d", WEXITSTATUS(status));

        // Signal threads to exit
        shared_data->should_exit = 1;

        // Wake up sender thread if it's blocked waiting for messages
        sem_post(get_message_sem());

        // Wait for threads
        pthread_join(receiver, NULL);
        pthread_join(sender, NULL);
        pthread_join(ack_checker, NULL);

        // Check if exit was due to ACK timeout
        if (shared_data->ack_timeout_occurred)
        {
            LOG_WARNING_MSG("Disconnection due to ACK timeout");
        }

        ipc_cleanup();
    }

    LOG_INFO_MSG("Client logic shutdown complete");
    return 0;
}
