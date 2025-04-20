#include "dhl_client.h"
#include "config.h"
#include "facade.h"
#include "inventory.h"
#include "logger.h"

#define PATH_LOG "logs/client.log"
#define CLIENT "CLIENT"

int start()
{
    init_params_client params = load_config_client(PATH_CONFIG, SECTION);
    generate_and_set_client_id(params.client_type);
    printf("Client: %s\n", get_identifiers()->client_id);
    printf("Client SHM path: %s\n", get_identifiers()->shm_path);
    log_init(PATH_LOG, CLIENT);
    set_log_level(LOG_LEVEL_DEBUG);
    log_info("Client started with ID: %s", get_identifiers()->client_id);
    init_inventory_random();
    connection(params);
    return 0;
}
