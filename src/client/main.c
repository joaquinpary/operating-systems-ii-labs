#include "dhl_client.h"
#include <stdio.h>
#include <string.h>

#define CLI "cli"

int main(int argc, char* argv[])
{
    if (argc < 2)
        return 1;
    if (!strcmp(argv[1], CLI))
    {
        printf("Client: %s\n", argv[1]);
        if (start_cli())
            return 1;
    }
    else
    {
        printf("Client: %s\n", argv[1]);
        if (start_client(argv[1]))
            return 1;
    }
    return 0;
}
