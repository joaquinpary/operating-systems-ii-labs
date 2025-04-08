#include "json_manager.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 256
#define MIN_SIZE 64

init_params_client load_config_client(const char* FILENAME, const char* SECTION)
{
    init_params_client params = {0};
    FILE* file = fopen(FILENAME, "r");
    if (!file)
    {
        perror("Error opening file");
        return params;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    char* data = malloc(file_size + 1);
    fread(data, 1, file_size, file);
    data[file_size] = '\0';
    fclose(file);

    cJSON* json = cJSON_Parse(data);
    if (!json)
    {
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        free(data);
        return params;
    }
    cJSON* section_obj = cJSON_GetObjectItemCaseSensitive(json, SECTION);
    if (!section_obj)
    {
        fprintf(stderr, "Section '%s' not found in configuration\n", SECTION);
        cJSON_Delete(json);
        free(data);
        return params;
    }

    cJSON* host = cJSON_GetObjectItemCaseSensitive(section_obj, "host");
    cJSON* port = cJSON_GetObjectItemCaseSensitive(section_obj, "port");
    cJSON* ip_version = cJSON_GetObjectItemCaseSensitive(section_obj, "ip_version");
    cJSON* protocol = cJSON_GetObjectItemCaseSensitive(section_obj, "protocol");

    if (cJSON_IsString(host) && cJSON_IsString(port) && cJSON_IsString(ip_version) && cJSON_IsString(protocol))
    {
        strncpy(params.host, host->valuestring, BUFFER_SIZE - 1);
        strncpy(params.port, port->valuestring, MIN_SIZE - 1);
        strncpy(params.ip_version, ip_version->valuestring, MIN_SIZE - 1);
        strncpy(params.protocol, protocol->valuestring, MIN_SIZE - 1);
    }
    else
    {
        fprintf(stderr, "Invalid configuration format.\n");
        cJSON_Delete(json);
        free(data);
        return params;
    }

    cJSON_Delete(json);
    free(data);

    return params;
}
