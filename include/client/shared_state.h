#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "json_manager.h"

#define ITEMS 6
#define ACTIONS 2

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

typedef struct
{
    inventory_item items[ITEMS];
    inventory_item items_to_send[ITEMS];
    int finish;
    int timer_tick;
    int next_action[ACTIONS];
} shared_data;

void sem_wait();
void sem_signal();
int init_shared_memory();
shared_data* get_shared_data();
int get_semaphore_id();
int get_inventory_size();
inventory_item* get_inventory();
inventory_item* get_inventory_to_send();
inventory_item* get_inventory_to_replenish();
void init_inventory();
void set_inventory_to_send(inventory_item* items_to_send);
void set_inventory(inventory_item* items);
int replenish();
#endif
