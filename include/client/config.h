#ifndef CONFIG_H
#define CONFIG_H

#include "json_manager.h"

#define SHM_PATH_SIZE 128
typedef struct
{
    char client_id[MIN_SIZE];
    char session_token[SESSION_TOKEN_SIZE];
    char shm_path[SHM_PATH_SIZE];
    char client_type[MIN_SIZE];
} identifiers;

identifiers* get_identifiers();
void set_session_token(const char* session_token);
void set_client_id(const char* client_id);
void set_client_type(const char* client_type);

#endif
