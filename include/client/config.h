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
    char protocol[MIN_SIZE];
    char username[MIN_SIZE];
    char password[MIN_SIZE];

} identifiers;

identifiers* get_identifiers();
void set_session_token(const char* session_token);
void set_client_id(const char* client_id);
void set_client_type(const char* client_type);
void set_protocol(const char* protocol);
void set_username(const char* username);
void set_password(const char* password);
void set_params(init_params_client params);

#endif
