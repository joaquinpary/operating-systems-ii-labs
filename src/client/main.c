#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[])
{
    if (argc >= 3 && strcmp(argv[1], "--config") == 0)
    {
        char path[256];
        snprintf(path, sizeof(path), "config/clients/client_%s.conf", argv[2]);
        return run_client(path);
    }
    return 1;
}
