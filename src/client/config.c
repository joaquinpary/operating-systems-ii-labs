#include "config.h"
#include "json_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static identifiers ident = {0};

identifiers* get_identifiers()
{
    return &ident;
}

void set_session_token(const char* session_token)
{
    strncpy(ident.session_token, session_token, SESSION_TOKEN_SIZE - 1);
    ident.session_token[SESSION_TOKEN_SIZE - 1] = '\0';
}

void set_client_id(const char* client_id)
{
    strncpy(ident.client_id, client_id, MIN_SIZE - 1);
    ident.client_id[MIN_SIZE - 1] = '\0';

    snprintf(ident.shm_path, sizeof(ident.shm_path), "/tmp/shm_client_%s", ident.client_id);

    FILE* f = fopen(ident.shm_path, "w");
    if (f)
        fclose(f);
    else
        perror("Error creating shm path file");
}

void generate_prefixed_client_id(const char* prefix, char* client_type, size_t size)
{
    if (!prefix || !client_type || size < 16)
    {
        fprintf(stderr, "Invalid arguments for client ID generation\n");
        return;
    }

    srand(time(NULL) ^ getpid());
    int unique_number = rand() % 10000;
    snprintf(client_type, size, "%s%04d", prefix, unique_number);
}

void generate_and_set_client_id(const char* prefix)
{
    char client_id[MIN_SIZE];
    generate_prefixed_client_id(prefix, client_id, sizeof(client_id));
    set_client_id(client_id);
}
