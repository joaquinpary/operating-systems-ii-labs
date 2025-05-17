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

void sem_wait();
void sem_signal();
int init_shared_memory();
void cleanup_shared_memory();
shared_data* get_shared_data();
int get_semaphore_id();
int get_inventory_size();
inventory_item* get_inventory();
inventory_item* get_inventory_to_send();
inventory_item* get_inventory_received();
inventory_item* get_inventory_to_replenish();
void init_inventory();
void set_inventory_to_send(inventory_item* items_to_send);
void set_inventory(inventory_item* items);
void set_inventory_received(inventory_item* items);
int replenish();
double get_uniform_random(double min, double max);
int inventory_compsumption();
void set_hub_username(const char* username);
void set_warehouse_username(const char* username);
char* get_hub_username();
char* get_warehouse_username();
#endif
