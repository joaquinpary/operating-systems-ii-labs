#ifndef INVENTORY_H
#define INVENTORY_H

#include "json_manager.h"

/*
 * @brief
 * Function to get the size of the inventory.
 * @return The size of the inventory.
 */
int get_inventory_size();

/*
 * @brief
 * Function to get the full inventory.
 * @return The full inventory.
 */
inventory_item* get_full_inventory();

/*
 * @brief
 * Function to get the full inventory to send.
 * @return The full inventory to send.
 */
inventory_item* get_full_inventory_to_send();

/*
 * @brief
 * Function to get an item from the inventory by name.
 * @param name The name of the item.
 * @return The inventory item with the specified name.
 */
inventory_item get_item_by_name(const char* name);

/*
 * @brief
 * Function to set the quantity of an item in the inventory.
 * @param items The items to set the quantity for.
 * @param add_or_subtract 1 to add, 0 to subtract.
 */
void set_item_quantity(inventory_item* items, int add_or_subtract);

/*
 * @brief
 * Function to get the inventory item by name.
 * @param items The items to set the quantity for.
 * @return The inventory item with the specified name.
 */
void set_item_quantity_to_send(inventory_item* items);
/*
 * @brief
 * Function to verify the inventory.
 * @return 1 if the inventory is valid, 0 otherwise.
 */
int verify_inventory();

/*
 * @brief
 * Function to get the items to replenish in the inventory.
 * @param to_order The items to order.
 */
void get_items_to_replenish(inventory_item* to_order);

/*
 * @brief
 * Function to compare the inventory with another inventory.
 * @param other_inventory The other inventory to compare with.
 * @return 1 if the inventories are equal, 0 otherwise.
 */
int compare_inventory(inventory_item* other_inventory);

/*
 * @brief
 * Function to initialize the inventory with random values.
 */
void init_inventory_random();

#endif
