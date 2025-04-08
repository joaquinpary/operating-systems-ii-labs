#ifndef JSON_MANAGER_H
#define JSON_MANAGER_H

#include "cJSON.h"

#define BUFFER_SIZE 256
#define MIN_SIZE 64

typedef struct
{
    char host[BUFFER_SIZE];
    char port[MIN_SIZE];
    char ip_version[MIN_SIZE];
    char protocol[MIN_SIZE];
} init_params_client;

init_params_client load_config_client(const char* FILENAME, const char* SECTION);

#endif
