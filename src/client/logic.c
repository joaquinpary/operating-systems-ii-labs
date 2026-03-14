#define _POSIX_C_SOURCE 200809L
#include "logic.h"
#include "connection.h"
#include "emergency_detector.h"
#include "json_manager.h"
#include "logger.h"
#include "message_handler.h"
#include "shared_state.h"
#include <dlfcn.h>
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
#define CONSUME_STOCK_MIN_SEC 10          // Minimum seconds between stock consumption
#define CONSUME_STOCK_MAX_SEC 30         // Maximum seconds between stock consumption
#define CONSUME_MIN_AMOUNT 1             // Minimum units to consume per item
#define CONSUME_MAX_AMOUNT 20            // Maximum units to consume per item
#define LOW_STOCK_THRESHOLD 20           // Threshold to trigger stock request
#define CRITICAL_STOCK_THRESHOLD 5       // Threshold to disable consumption
#define MAX_STOCK_PER_ITEM 100           // Maximum inventory capacity per item
#define EMERGENCY_CHECK_INTERVAL_SEC 30  // Interval for emergency evaluation

static client_context* logic_ctx = NULL;

// ==================== EMERGENCY LIBRARY (dlopen) ====================

typedef emergency_result_t (*evaluate_fn_t)(const emergency_config_t*);

static void* emergency_lib_handle = NULL;
static evaluate_fn_t evaluate_fn = NULL;

/**
 * @brief Load libemergency.so at runtime via dlopen
 * @return 0 on success, -1 if the library or symbol could not be loaded
 */
static int load_emergency_library(void)
{
    emergency_lib_handle = dlopen("libemergency.so", RTLD_LAZY);
    if (!emergency_lib_handle)
    {
        LOG_ERROR_MSG("dlopen(libemergency.so) failed: %s", dlerror());
        return -1;
    }

    dlerror(); /* clear any existing error */

    evaluate_fn = (evaluate_fn_t)dlsym(emergency_lib_handle, "evaluate_emergency");
    const char* dl_err = dlerror();
    if (dl_err)
    {
        LOG_ERROR_MSG("dlsym(evaluate_emergency) failed: %s", dl_err);
        dlclose(emergency_lib_handle);
        emergency_lib_handle = NULL;
        return -1;
    }

    /* Log library version if symbol is available */
    typedef const char* (*version_fn_t)(void);
    version_fn_t version_fn = (version_fn_t)dlsym(emergency_lib_handle, "emergency_lib_version");
    if (!dlerror() && version_fn)
    {
        LOG_INFO_MSG("libemergency.so loaded (version %s)", version_fn());
    }

    return 0;
}

/**
 * @brief Evaluate emergency probability and enqueue alert if triggered
 *
 * Uses the dynamically-loaded evaluate_emergency() function. If an emergency
 * is detected and no emergency is already active, constructs and enqueues a
 * CLIENT_EMERGENCY_ALERT message.
 */
static void do_emergency_check(void)
{
    if (!evaluate_fn)
    {
        return;
    }

    shared_data_t* shared_data = get_shared_data();

    if (shared_data->emergency_active)
    {
        LOG_DEBUG_MSG("Emergency already active, skipping evaluation");
        return;
    }

    emergency_result_t result = evaluate_fn(NULL); /* NULL → use default 2% probability */

    if (result.emergency_code == EMERGENCY_CODE_NONE)
    {
        return;
    }

    LOG_WARNING_MSG("Emergency detected: code=%d type=%s severity=%d", result.emergency_code, result.emergency_type,
                    result.severity);

    message_t msg;
    if (create_client_emergency_message(&msg, shared_data->client_role, shared_data->client_id, result.emergency_code,
                                        result.emergency_type) != 0)
    {
        LOG_ERROR_MSG("Failed to create emergency alert message");
        return;
    }

    if (enqueue_pending_message(&msg) != 0)
    {
        LOG_ERROR_MSG("Failed to enqueue emergency alert message");
        return;
    }

    shared_data->emergency_active = 1;
    LOG_INFO_MSG("Emergency alert enqueued: %s (code %d)", result.emergency_type, result.emergency_code);
}

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

    LOG_INFO_MSG("ACK checker thread started");

    while (!shared_data->should_exit)
    {
        int result = check_ack_timeouts();

        if (result < 0)
        {
            LOG_ERROR_MSG("Max retries exceeded, triggering shutdown");
            shared_data->should_exit = 1;
            wake_sender_thread();
            break;
        }
        else if (result > 0)
        {
            char messages_to_retry[MAX_PENDING_ACKS][BUFFER_SIZE];
            int retry_count = collect_retryable_messages(messages_to_retry, MAX_PENDING_ACKS);

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

    LOG_INFO_MSG("Sender thread started");

    while (!shared_data->should_exit)
    {
        int sem_result = wait_for_message(1);

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
        else if (sem_result == 1)
        {
            continue;
        }
        else
        {
            LOG_ERROR_MSG("wait_for_message error");
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

    struct timespec jitter = {.tv_sec = rand() % 3, .tv_nsec = (long)(rand() % 1000) * 1000000L};
    nanosleep(&jitter, NULL);

    inventory_item_t items[QUANTITY_ITEMS];

    if (get_inventory_snapshot(items) != 0)
    {
        LOG_ERROR_MSG("Failed to get inventory snapshot");
        return;
    }

    log_inventory_snapshot("INVENTORY_UPDATE send");

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
    // Prepare items to reduce with random amounts
    inventory_item_t inventory_snapshot[QUANTITY_ITEMS];
    inventory_item_t items_to_reduce[QUANTITY_ITEMS];
    int items_with_stock = 0;
    int total_to_consume = 0;

    if (get_inventory_snapshot(inventory_snapshot) != 0)
    {
        LOG_ERROR_MSG("Failed to get inventory snapshot for consumption");
        return;
    }

    // Build list of items to consume with random amounts
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        items_to_reduce[i].item_id = inventory_snapshot[i].item_id;
        strncpy(items_to_reduce[i].item_name, inventory_snapshot[i].item_name, ITEM_NAME_SIZE - 1);
        items_to_reduce[i].item_name[ITEM_NAME_SIZE - 1] = '\0';

        if (inventory_snapshot[i].quantity > 0)
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

    // Warehouses replenish only through dispatch orders from the server,
    // not through autonomous stock requests.
    if (strcmp(shared_data->client_role, HUB) != 0)
    {
        LOG_DEBUG_MSG("do_check_low_stock() skipped for WAREHOUSE role");
        return 0;
    }

    inventory_item_t low_items[QUANTITY_ITEMS];
    int requested_item_indices[QUANTITY_ITEMS];
    int critically_low_count = 0;

    int low_count = get_low_stock_report(LOW_STOCK_THRESHOLD, CRITICAL_STOCK_THRESHOLD, MAX_STOCK_PER_ITEM, low_items,
                                         requested_item_indices, &critically_low_count);
    if (low_count < 0)
    {
        LOG_ERROR_MSG("Failed to evaluate low stock report");
        return 0;
    }

    for (int i = 0; i < low_count; i++)
    {
        LOG_DEBUG_MSG("Low stock detected: %s (ID: %d), requesting %d units", low_items[i].item_name,
                      low_items[i].item_id, low_items[i].quantity);
    }

    if (low_count > 0)
    {
        LOG_INFO_MSG("Low stock detected for %d items, requesting replenishment", low_count);

        message_t msg;
        inventory_item_t full_request_items[QUANTITY_ITEMS];

        if (build_full_request_payload(low_items, low_count, full_request_items) != 0)
        {
            LOG_ERROR_MSG("Failed to build full stock request payload");
            return critically_low_count == QUANTITY_ITEMS ? 1 : 0;
        }

        log_inventory_snapshot("STOCK_REQUEST send");

        if (create_items_message(&msg, HUB_TO_SERVER__STOCK_REQUEST, shared_data->client_id, SERVER, full_request_items,
                                 QUANTITY_ITEMS, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create stock request message");
            return critically_low_count == QUANTITY_ITEMS ? 1 : 0;
        }

        if (enqueue_pending_message(&msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue stock request");
            return critically_low_count == QUANTITY_ITEMS ? 1 : 0;
        }

        mark_pending_stock_requests(requested_item_indices, low_count);

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

    /* Load emergency detection library */
    if (load_emergency_library() != 0)
    {
        LOG_WARNING_MSG("Emergency library unavailable — emergency alerts disabled");
    }

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

    int emergency_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (emergency_timer_fd == -1)
    {
        LOG_ERROR_MSG("Failed to create emergency timer: %s", strerror(errno));
        close(inventory_timer_fd);
        close(consume_timer_fd);
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
        close(emergency_timer_fd);
        return -1;
    }

    // Configure emergency check timer (30 seconds, repeating)
    struct itimerspec emergency_spec = {.it_interval = {.tv_sec = EMERGENCY_CHECK_INTERVAL_SEC, .tv_nsec = 0},
                                        .it_value = {.tv_sec = EMERGENCY_CHECK_INTERVAL_SEC, .tv_nsec = 0}};
    if (timerfd_settime(emergency_timer_fd, 0, &emergency_spec, NULL) == -1)
    {
        LOG_ERROR_MSG("Failed to set emergency timer: %s", strerror(errno));
        close(inventory_timer_fd);
        close(consume_timer_fd);
        close(emergency_timer_fd);
        return -1;
    }

    LOG_INFO_MSG("Timers configured: inventory=%ds, consume=random(%d-%ds), first=%ds, emergency=%ds",
                 INVENTORY_UPDATE_INTERVAL_SEC, CONSUME_STOCK_MIN_SEC, CONSUME_STOCK_MAX_SEC, consume_interval,
                 EMERGENCY_CHECK_INTERVAL_SEC);

    // Event loop with select()
    int maxfd = inventory_timer_fd;
    if (consume_timer_fd > maxfd)
        maxfd = consume_timer_fd;
    if (emergency_timer_fd > maxfd)
        maxfd = emergency_timer_fd;
    fd_set readfds;
    int consume_timer_active = 1; // Track if consume timer is active

    while (!shared_data->should_exit)
    {
        if (shared_data->inventory_updated && !consume_timer_active)
        {
            shared_data->inventory_updated = 0; // Reset flag

            int items_with_stock = count_items_above_threshold(5);

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
        FD_SET(emergency_timer_fd, &readfds);

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

            // Check emergency evaluation timer
            if (FD_ISSET(emergency_timer_fd, &readfds))
            {
                if (read(emergency_timer_fd, &expirations, sizeof(expirations)) > 0)
                {
                    LOG_DEBUG_MSG("Emergency timer expired (%lu times)", expirations);
                    do_emergency_check();
                }
            }
        }
    }

    close(inventory_timer_fd);
    close(consume_timer_fd);
    close(emergency_timer_fd);

    if (emergency_lib_handle)
    {
        dlclose(emergency_lib_handle);
        emergency_lib_handle = NULL;
    }

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
            wake_sender_thread();
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
        wake_sender_thread();

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
