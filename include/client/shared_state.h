#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "json_manager.h"
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>

// Configuration constants
#define MAX_PENDING_ACKS 10
#define ACK_TIMEOUT_SECONDS 5
#define MAX_RETRIES 3

// ==================== DATA STRUCTURES ====================

// Structure to track pending ACKs
typedef struct
{
    time_t send_time;
    char msg_id[TIMESTAMP_SIZE];
    char msg_type[MESSAGE_TYPE_SIZE];
    char message_json[BUFFER_SIZE];
    int retry_count;
    int active;
} pending_ack_t;

// Shared memory structure
typedef struct
{
    char client_role[ROLE_SIZE]; // "HUB" or "WAREHOUSE"
    char client_id[ID_SIZE];     // username (e.g., "client_0001")

    inventory_item_t inventory_item[QUANTITY_ITEMS];
    int pending_stock_request[QUANTITY_ITEMS]; // 1 if item already requested and waiting for replenishment

    char pending_messages[10][BUFFER_SIZE];
    int message_count;

    // ACK tracking
    pending_ack_t pending_acks[MAX_PENDING_ACKS];
    int ack_timeout_occurred;

    // Inventory control flags
    volatile sig_atomic_t inventory_updated; // Flag set when inventory is updated

    // Emergency control flags
    volatile sig_atomic_t emergency_active; // Flag set while an emergency alert is pending ACK

    // Control flags
    volatile sig_atomic_t should_exit;
} shared_data_t;

// ==================== IPC LIFECYCLE ====================

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

// ==================== MESSAGE QUEUE ====================

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

// ==================== ACK TRACKING ====================

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

// ==================== INVENTORY ====================

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
    INVENTORY_ADD,   // Add quantities to inventory (restocking)
    INVENTORY_REDUCE // Reduce quantities from inventory (consumption/dispatch)
} inventory_operation_t;

int modify_inventory(const inventory_item_t* items, inventory_operation_t operation);

/**
 * Get inventory count for an item by ID
 * @param item_id Item ID to query
 * @return Item count, or -1 if not found
 */
int get_inventory_count(int item_id);

// ==================== SHARED DATA ACCESS ====================

/**
 * Get pointer to shared data
 * @return Pointer to shared_data_t
 */
shared_data_t* get_shared_data(void);

/**
 * Get inventory semaphore
 * @return Pointer to inventory semaphore
 */
sem_t* get_inventory_sem(void);

/**
 * Get message available semaphore
 * @return Pointer to message available semaphore
 */
sem_t* get_message_sem(void);

#endif
