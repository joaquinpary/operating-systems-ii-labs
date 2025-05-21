#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "json_manager.h"

#define ITEMS 6

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

typedef struct
{
    inventory_item items[ITEMS];
    inventory_item items_to_send[ITEMS];
    inventory_item items_received[ITEMS];
    char hub_username[USER_PASS_SIZE];
    char warehouse_username[USER_PASS_SIZE];
} shared_data;

/* @brief
 * Function to wait for a semaphore.
 */
void sem_wait();

/* @brief
 * Function to signal a semaphore.
 */
void sem_signal();

/* @brief
 * Function to initialize the shared memory.
 * @return 0 on success, 1 on failure.
 */
int init_shared_memory();

/* @brief
 * Function to cleanup the shared memory.
 * @return 0 on success, 1 on failure.
 */
void cleanup_shared_memory();

/* @brief
 * Function to get the shared data.
 * @return The shared data.
 */
shared_data* get_shared_data();

/* @brief
 * Function to get the semaphore ID.
 * @return The semaphore ID.
 */
int get_semaphore_id();

/* @brief
 * Function to get the inventory size.
 * @return The inventory size.
 */
int get_inventory_size();

/* @brief
 * Function to get the inventory.
 * @return The inventory.
 */
inventory_item* get_inventory();

/* @brief
 * Function to get the inventory to send.
 * @return The inventory to send.
 */
inventory_item* get_inventory_to_send();

/* @brief
 * Function to get the inventory received.
 * @return The inventory received.
 */
inventory_item* get_inventory_received();

/* @brief
 * Function to get the inventory to replenish.
 * @return The inventory to replenish.
 */
inventory_item* get_inventory_received();
inventory_item* get_inventory_to_replenish();

/* @brief
 * Function to initialize the inventory.
 */
void init_inventory();

/* @brief
 * Function to set the inventory to send.
 * @param items_to_send The inventory to send.
 */
void set_inventory_to_send(inventory_item* items_to_send);

/* @brief
 * Function to set the inventory.
 */
void set_inventory(inventory_item* items);

/* @brief
 * Function to set the inventory received.
 */
void set_inventory_received(inventory_item* items);

/* @brief
 * Function to replenish the inventory.
 * @return 0 for not replenished, 1 for replenished.
 */
int replenish();

/* @brief
 * Function to get a uniform random number.
 * @param min The minimum value.
 * @param max The maximum value.
 * @return The uniform random number.
 */
double get_uniform_random(double min, double max);

/* @brief
 * Function to set the hub username.
 * @param username The hub username.
 */
void set_hub_username(const char* username);

/* @brief
 * Function to set the warehouse username.
 * @param username The warehouse username.
 */
void set_warehouse_username(const char* username);

/* @brief
 * Function to get the hub username.
 * @return The hub username.
 */
char* get_hub_username();

/* @brief
 * Function to get the warehouse username.
 * @return The warehouse username.
 */
char* get_warehouse_username();

#endif
