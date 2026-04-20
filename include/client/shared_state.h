#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "json_manager.h"
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>

#define MAX_PENDING_ACKS 10
#define MAX_PENDING_MESSAGES 10

typedef struct
{
    struct timespec send_time;
    char msg_id[TIMESTAMP_SIZE];
    char msg_type[MESSAGE_TYPE_SIZE];
    char message_json[BUFFER_SIZE];
    int retry_count;
    int active;
} pending_ack_t;

typedef struct
{
    char client_role[ROLE_SIZE];
    char client_id[ID_SIZE];

    inventory_item_t inventory_item[QUANTITY_ITEMS];
    int pending_stock_request[QUANTITY_ITEMS];

    char pending_messages[MAX_PENDING_MESSAGES][BUFFER_SIZE];
    int message_count;

    pending_ack_t pending_acks[MAX_PENDING_ACKS];
    int ack_timeout_occurred;

    volatile sig_atomic_t inventory_updated;

    volatile sig_atomic_t emergency_active;

    volatile sig_atomic_t should_exit;
} shared_data_t;

/**
 * Initialize IPC resources (shared memory and semaphores)
 * @param client_id Unique client identifier (e.g., "client_0001") for namespace isolation
 * @return 0 on success, -1 on error
 */
int ipc_init(const char* client_id);

/**
 * Cleanup IPC resources
 */
void ipc_cleanup(void);

/**
 * Add a message to the pending queue (serializes message_t to JSON)
 * @param msg Message structure to enqueue
 * @return 0 on success, -1 on error
 */
int enqueue_pending_message(const message_t* msg);

/**
 * Add a JSON message to the pending queue (no serialization)
 * @param json_message JSON string to enqueue
 * @return 0 on success, -1 on error
 */
int enqueue_pending_message_json(const char* json_message);

/**
 * Remove and deserialize the next message from the queue
 * @param msg Output message structure
 * @return 0 on success, -1 if queue is empty
 */
int pop_pending_message(message_t* msg);

/**
 * Check if there are pending messages
 * @return 1 if messages exist, 0 otherwise
 */
int has_pending_messages(void);

/**
 * Add a pending ACK for tracking
 * @param msg_id Message ID (timestamp)
 * @param msg_type Message type for unique identification
 * @param message_json Original JSON message for retransmission
 * @return 0 on success, -1 if no free slots
 */
int add_pending_ack(const char* msg_id, const char* msg_type, const char* message_json);

/**
 * Remove a pending ACK (when ACK is received)
 * @param msg_id Message ID to remove
 * @return 0 on success, -1 if not found
 */
int remove_pending_ack(const char* msg_id);

/**
 * Check for ACK timeouts and update retry counts
 * @return -1 if max retries exceeded (disconnect), 1 if needs retransmission, 0 if ok
 */
int check_ack_timeouts(void);

/**
 * Modify inventory by adding or reducing quantities (thread-safe)
 *
 * ADD mode: Adds quantities to existing inventory (for restocking).
 * REDUCE mode: Reduces inventory, consuming available stock (handles partial depletion).
 *
 * @param items Array of QUANTITY_ITEMS with quantities to add/reduce
 * @param operation INVENTORY_ADD or INVENTORY_REDUCE
 * @return 0 on success, -1 on error
 */
typedef enum
{
    INVENTORY_ADD,
    INVENTORY_REDUCE
} inventory_operation_t;

int modify_inventory(const inventory_item_t* items, inventory_operation_t operation);

/**
 * Get inventory count for an item by ID
 * @param item_id Item ID to query
 * @return Item count, or -1 if not found
 */
int get_inventory_count(int item_id);

/**
 * Get a thread-safe snapshot of the full inventory
 * @param out_items Output array with QUANTITY_ITEMS entries
 * @return 0 on success, -1 on error
 */
int get_inventory_snapshot(inventory_item_t* out_items);

/**
 * Count items with stock >= threshold
 * @param threshold Minimum quantity threshold
 * @return Number of items meeting threshold, -1 on error
 */
int count_items_above_threshold(int threshold);

/**
 * Build low-stock report excluding already-pending requests
 * @param low_threshold Threshold to include an item in low-stock list
 * @param critical_threshold Threshold to count critically low items
 * @param max_stock Target stock level to calculate needed quantity
 * @param out_low_items Output array for low-stock items
 * @param out_item_indices Output array with source inventory indices
 * @param out_critical_count Output count of critically low items
 * @return Number of low-stock items in out_low_items, -1 on error
 */
int get_low_stock_report(int low_threshold, int critical_threshold, int max_stock, inventory_item_t* out_low_items,
                         int* out_item_indices, int* out_critical_count);

/**
 * Log the current inventory state at DEBUG level
 * @param context Label shown in the log line to identify the call site
 */
void log_inventory_snapshot(const char* context);

/**
 * Build a full QUANTITY_ITEMS payload from a low-stock subset
 *
 * Preserves canonical inventory item identity (item_id + item_name) for all items.
 * Items present in low_items keep their requested quantity; the rest are set to 0.
 *
 * @param low_items Subset array returned by get_low_stock_report
 * @param low_count Number of entries in low_items
 * @param out_full_items Output array with QUANTITY_ITEMS entries
 * @return 0 on success, -1 on error
 */
int build_full_request_payload(const inventory_item_t* low_items, int low_count, inventory_item_t* out_full_items);

/**
 * Mark requested items as pending replenishment
 * @param item_indices Array of inventory indices [0..QUANTITY_ITEMS-1]
 * @param count Number of indices in item_indices
 */
void mark_pending_stock_requests(const int* item_indices, int count);

/**
 * Clear pending replenishment flag for delivered items
 * @param items Array of delivered items
 * @param count Number of entries in items
 */
void clear_pending_stock_requests(const inventory_item_t* items, int count);

/**
 * Copy retryable pending ACK payloads for retransmission
 * @param out_messages Output 2D buffer [max_count][BUFFER_SIZE]
 * @param max_count Max messages to copy
 * @return Number of copied messages
 */
int collect_retryable_messages(char out_messages[][BUFFER_SIZE], int max_count);

/**
 * Wait for pending message availability with timeout
 * @param timeout_seconds Timeout in seconds
 * @return 0 if message is available, 1 on timeout/interruption, -1 on error
 */
int wait_for_message(int timeout_seconds);

/**
 * Wake sender thread blocked on wait_for_message
 */
void wake_sender_thread(void);

/**
 * Get pointer to shared data
 * @return Pointer to shared_data_t
 */
shared_data_t* get_shared_data(void);

/**
 * Legacy direct semaphore accessors (prefer wrapper APIs above)
 */
sem_t* get_inventory_sem(void);
sem_t* get_message_sem(void);

#endif
