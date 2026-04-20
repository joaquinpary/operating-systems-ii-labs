#ifndef CLIENT_H
#define CLIENT_H

typedef struct
{
    char type[32];
    char username[64];
    char password[64];
} client_credentials;

/**
 * @brief Main function to run the client application.
 *
 * This function initializes the logger, loads configuration, establishes a connection,
 * performs authentication, and starts the client logic. It handles cleanup on exit.
 *
 * @param config_path Path to the client configuration file.
 * @return 0 on success, non-zero on failure.
 */

int run_client(const char* config_path);

#endif
