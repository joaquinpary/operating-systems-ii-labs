#include "inventory.h"
#include "json_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WATER "water"
#define FOOD "food"
#define MEDICINE "medicine"
#define GUNS "guns"
#define AMMO "ammo"
#define TOOLS "tools"

#define MAX_ITEM_PER_TYPE 100
#define MIN_ITEM_PER_TYPE 20
#define ITEMS 6

static inventory_item inventory[] = {{WATER, 0}, {FOOD, 0}, {MEDICINE, 0}, {GUNS, 0}, {AMMO, 0}, {TOOLS, 0}};
static inventory_item inventory_to_send[] = {{WATER, 0}, {FOOD, 0}, {MEDICINE, 0}, {GUNS, 0}, {AMMO, 0}, {TOOLS, 0}};

int get_inventory_size()
{
    return sizeof(inventory) / sizeof(inventory_item);
}

inventory_item* get_full_inventory()
{
    return inventory;
}

inventory_item* get_full_inventory_to_send()
{
    return inventory_to_send;
}

inventory_item get_item_by_name(const char* name)
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        if (strcmp(inventory[i].item, name) == 0)
        {
            return inventory[i];
        }
    }
    inventory_item not_found = {"", -1};
    return not_found;
}

void set_item_quantity(inventory_item* items, int add_or_subtract)
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        for (int j = 0; j < get_inventory_size(); ++j)
        {
            if (strcmp(inventory[i].item, items[j].item) == 0)
            {
                if (add_or_subtract == 1)
                    inventory[i].quantity += items[j].quantity;
                else
                    inventory[i].quantity -= items[j].quantity;
            }
        }
    }
    return;
}

void set_item_quantity_to_send(inventory_item* items)
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        for (int j = 0; j < get_inventory_size(); ++j)
        {
            if (strcmp(inventory_to_send[i].item, items[j].item) == 0)
            {
                inventory_to_send[i].quantity = items[j].quantity;
            }
        }
    }
    return;
}

int verify_inventory()
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        if (inventory[i].quantity < MIN_ITEM_PER_TYPE)
            return 0;
    }
    return 1;
}

void get_items_to_replenish(inventory_item* to_order)
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        strcpy(to_order[i].item, inventory[i].item);
        if (inventory[i].quantity <= MIN_ITEM_PER_TYPE)
        {
            to_order[i].quantity = MAX_ITEM_PER_TYPE - inventory[i].quantity;
        }
        else
        {
            to_order[i].quantity = 0;
        }
    }
    return;
}

int compare_inventory(inventory_item* other_inventory)
{
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        if (inventory[i].quantity != other_inventory[i].quantity)
        {
            return 1;
        }
    }
    return 0;
}

void init_inventory_random()
{
    srand(time(NULL));
    for (int i = 0; i < get_inventory_size(); i++)
    {
        inventory[i].quantity = rand() % 101;
        // inventory[i].quantity = 0;
    }
    return;
}
