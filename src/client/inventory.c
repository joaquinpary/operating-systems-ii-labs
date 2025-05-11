#include "inventory.h"
#include "config.h"
#include "json_manager.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14
#endif

#ifndef M_PI
#define M_PI 3.14
#endif

#define WATER "water"
#define FOOD "food"
#define MEDICINE "medicine"
#define GUNS "guns"
#define AMMO "ammo"
#define TOOLS "tools"
#define SHM_SIZE 1024 // Segment size for shared memory (may be adjusted for a int)
#define SHM_KEY 50
#define SEM_KEY 51
#define SEM_NUM 0
#define SEM_SENDER 1

#define WAREHOUSE_MAX 400
#define HUB_MAX 100
#define WAREHOUSE_MIN WAREHOUSE_MAX * 0.2
#define HUB_MIN HUB_MAX * 0.2
#define RELATION 5 // Relation between warehouse and hub quantities
#define T_CONST 60
#define REPLENISH_TIME 1
#define DEMAND_HUB 80 / T_CONST

static calculation calc = {0};
static shared_data* shm_ptr = NULL;
static int semid = -1;

void sem_wait()
{
    struct sembuf sb = {SEM_NUM, -1, 0};
    semop(semid, &sb, 1);
}

void sem_signal()
{
    struct sembuf sb = {SEM_NUM, 1, 0};
    semop(semid, &sb, 1);
}

int init_shared_memory()
{
    key_t key = ftok(get_identifiers()->shm_path, SHM_KEY);
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    if (shmid == -1)
    {
        perror("Error to create shared memory");
        return 1;
    }

    shm_ptr = (shared_data*)shmat(shmid, NULL, 0);
    if (shm_ptr == (shared_data*)-1)
    {
        perror("Error to attach shared memory");
        return 1;
    }
    key_t sem_key = ftok(get_identifiers()->shm_path, SEM_KEY);
    semid = semget(sem_key, 2, 0666 | IPC_CREAT);
    if (semid == -1)
    {
        perror("semget failed");
        exit(1);
    }
    union semun sem_union;
    if (semctl(semid, 0, GETVAL) == 0)
    {
        sem_union.val = 1;
        if (semctl(semid, 0, SETVAL, sem_union) == -1)
        {
            perror("semctl failed to set semval");
            return 1;
        }
    }
    sem_wait();
    memset(shm_ptr, 0, sizeof(shared_data));
    init_inventory();
    sem_signal();
    if (!strcmp(get_identifiers()->client_type, "warehouse"))
    {
        calculate_parameters((double)RELATION, (double)DEMAND_HUB, (double)T_CONST, (double)REPLENISH_TIME);
    }
    else if (!strcmp(get_identifiers()->client_type, "hub"))
    {
        calculate_parameters(1, (double)DEMAND_HUB, (double)T_CONST, (double)REPLENISH_TIME);
    }

    return 0;
}

void cleanup_shared_memory()
{
    if (shmdt(shm_ptr) == -1)
    {
        perror("Error detaching shared memory");
    }
    if (shmctl(shmget(ftok(get_identifiers()->shm_path, SHM_KEY), SHM_SIZE, 0666), IPC_RMID, NULL) == -1)
    {
        perror("Error removing shared memory");
    }
}

int get_inventory_size()
{
    return sizeof(shm_ptr->items) / sizeof(inventory_item);
}

shared_data* get_shared_data()
{
    return shm_ptr;
}

int get_semaphore_id()
{
    return semid;
}

inventory_item* get_inventory()
{
    inventory_item* inventory = malloc(sizeof(inventory_item) * get_inventory_size());
    if (inventory == NULL)
    {
        perror("Error allocating memory for inventory");
        return NULL;
    }
    sem_wait();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        strcpy(inventory[i].item, shm_ptr->items[i].item);
        inventory[i].quantity = shm_ptr->items[i].quantity;
    }
    sem_signal();
    return inventory;
}

inventory_item* get_inventory_to_send()
{
    inventory_item* inventory_to_send = malloc(sizeof(inventory_item) * get_inventory_size());
    if (inventory_to_send == NULL)
    {
        perror("Error allocating memory for inventory");
        return NULL;
    }
    sem_wait();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        strcpy(inventory_to_send[i].item, shm_ptr->items_to_send[i].item);
        inventory_to_send[i].quantity = shm_ptr->items_to_send[i].quantity;
    }
    sem_signal();
    return inventory_to_send;
}

inventory_item* get_inventory_to_replenish()
{
    double missing;
    inventory_item* inventory_to_replenish = malloc(sizeof(inventory_item) * get_inventory_size());
    if (inventory_to_replenish == NULL)
    {
        perror("Error allocating memory for inventory");
        return NULL;
    }
    sem_wait();
    if (strcmp(get_identifiers()->client_type, "warehouse") == 0)
    {
        for (int i = 0; i < get_inventory_size(); ++i)
        {
            strcpy(inventory_to_replenish[i].item, shm_ptr->items[i].item);
            missing = calc.objective_s - shm_ptr->items[i].quantity;
            if (missing > 0)
            {
                inventory_to_replenish[i].quantity = (int)missing;
            }
            else
            {
                inventory_to_replenish[i].quantity = 0;
            }
        }
    }
    sem_signal();
    return inventory_to_replenish;
}

void init_inventory()
{
    srand(time(NULL));
    strncpy(shm_ptr->items[0].item, WATER, MIN_SIZE - 1);
    shm_ptr->items[0].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items[1].item, FOOD, MIN_SIZE - 1);
    shm_ptr->items[1].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items[2].item, MEDICINE, MIN_SIZE - 1);
    shm_ptr->items[2].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items[3].item, GUNS, MIN_SIZE - 1);
    shm_ptr->items[3].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items[4].item, AMMO, MIN_SIZE - 1);
    shm_ptr->items[4].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items[5].item, TOOLS, MIN_SIZE - 1);
    shm_ptr->items[5].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[0].item, WATER, MIN_SIZE - 1);
    shm_ptr->items_to_send[0].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[1].item, FOOD, MIN_SIZE - 1);
    shm_ptr->items_to_send[1].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[2].item, MEDICINE, MIN_SIZE - 1);
    shm_ptr->items_to_send[2].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[3].item, GUNS, MIN_SIZE - 1);
    shm_ptr->items_to_send[3].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[4].item, AMMO, MIN_SIZE - 1);
    shm_ptr->items_to_send[4].item[MIN_SIZE - 1] = '\0';
    strncpy(shm_ptr->items_to_send[5].item, TOOLS, MIN_SIZE - 1);
    shm_ptr->items_to_send[5].item[MIN_SIZE - 1] = '\0';
    for (int i = 0; i < get_inventory_size(); i++)
    {
        if (!strcmp(get_identifiers()->client_type, "warehouse"))
        {

            shm_ptr->items[i].quantity = WAREHOUSE_MAX;
            shm_ptr->items_to_send[i].quantity = 0;
        }
        else if (!strcmp(get_identifiers()->client_type, "hub"))
        {
            shm_ptr->items[i].quantity = HUB_MAX;
            shm_ptr->items_to_send[i].quantity = 0;
        }
    }
}

void calculate_parameters(double actors, double demand, double T, double rep_time)
{
    calc.demand_u = actors * demand * (T + rep_time);
    calc.sec_stock = 0.2 * calc.demand_u;
    calc.objective_s = calc.demand_u + calc.sec_stock;
}

void set_inventory_to_send(inventory_item* items_to_send)
{
    sem_wait();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        shm_ptr->items[i].quantity -= items_to_send[i].quantity;
        shm_ptr->items_to_send[i].quantity = items_to_send[i].quantity;
    }
    sem_signal();
}

void set_inventory(inventory_item* items)
{
    sem_wait();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        shm_ptr->items[i].quantity += items[i].quantity;
        if (shm_ptr->items[i].quantity > WAREHOUSE_MAX)
        {
            shm_ptr->items[i].quantity = WAREHOUSE_MAX;
        }
        else if (shm_ptr->items[i].quantity > HUB_MAX)
        {
            shm_ptr->items[i].quantity = HUB_MAX;
        }
    }
    sem_signal();
}

int replenish()
{
    double missing;
    inventory_item* items = malloc(sizeof(inventory_item) * get_inventory_size());
    if (items == NULL)
    {
        perror("Error allocating memory for items");
        return 1;
    }
    sem_wait();
    memcpy(items, shm_ptr->items, sizeof(inventory_item) * get_inventory_size());
    sem_signal();
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        missing = calc.objective_s - items[i].quantity;
        if (missing > 0)
        {
            return 1;
        }
    }
    return 0;
}

double get_uniform_random(double max, double min)
{
    double range = max - min;
    return min + ((double)rand() / (double)RAND_MAX) * range;
}

int inventory_compsumption()
{
    int unit = 0;
    for (int i = 0; i < get_inventory_size(); ++i)
    {
        unit = (int)get_uniform_random((DEMAND_HUB * 10) - 4, (DEMAND_HUB * 10) + 4);
        sem_wait();
        if (shm_ptr->items[i].quantity < unit)
            return 1;
        shm_ptr->items[i].quantity -= unit;
        sem_signal();
    }
    return 0;
}

void set_hub_username(const char* username)
{
    sem_wait();
    strncpy(shm_ptr->hub_username, username, USER_PASS_SIZE - 1);
    shm_ptr->hub_username[USER_PASS_SIZE - 1] = '\0';
    sem_signal();
}

char* get_hub_username()
{
    char* username = malloc(USER_PASS_SIZE);
    if (username == NULL)
    {
        perror("Error allocating memory for username");
        return NULL;
    }
    sem_wait();
    strncpy(username, shm_ptr->hub_username, USER_PASS_SIZE - 1);
    username[USER_PASS_SIZE - 1] = '\0';
    sem_signal();
    return username;
}
