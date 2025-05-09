#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "json_manager.h"

#define ITEMS 6
#define ACTIONS 2

typedef struct
{
    double demand_u;
    double objective_s;
    double sec_stock;

} calculation;

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

typedef struct
{
    inventory_item items[ITEMS];
    inventory_item items_to_send[ITEMS];
} shared_data;

void sem_wait();
void sem_signal();
// void sem_wait_sender();
// void sem_signal_sender();
int init_shared_memory();
void cleanup_shared_memory();
shared_data* get_shared_data();
int get_semaphore_id();
int get_inventory_size();
inventory_item* get_inventory();
inventory_item* get_inventory_to_send();
inventory_item* get_inventory_to_replenish();
void init_inventory();
void calculate_parameters(double actors, double demand, double T, double rep_time);
void set_inventory_to_send(inventory_item* items_to_send);
void set_inventory(inventory_item* items);
int replenish();
double get_uniform_random(double min, double max);
int inventory_compsumption();
#endif
