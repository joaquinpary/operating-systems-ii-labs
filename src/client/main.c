#include "client.h"
#include "logger.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Close logging resources and terminate on process shutdown signals.
 */
static void handle_shutdown(int sig)
{
    (void)sig;
    log_close();
    _exit(0);
}

#define CONFIG_PATH_DEFAULT "config/clients/client_%s.conf"

#define MAX_PATH_LENGTH 256

int main(int argc, char* argv[])
{
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);

    if (argc >= 3 && strcmp(argv[1], "--config") == 0)
    {
        char path[MAX_PATH_LENGTH];
        const char* config_base = getenv("CONFIG_BASE_PATH");
        if (config_base == NULL)
        {
            config_base = CONFIG_PATH_DEFAULT;
        }
        snprintf(path, sizeof(path), config_base, argv[2]);
        return run_client(path);
    }
    return 1;
}
