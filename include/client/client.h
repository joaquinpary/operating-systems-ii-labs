#ifndef CLIENT_H
#define CLIENT_H

typedef struct
{
    char type[32];
    char username[64];
    char password[64];
} client_credentials;

// Runs client by loading configuration from a .conf file
int run_client(const char* config_path);

#endif
