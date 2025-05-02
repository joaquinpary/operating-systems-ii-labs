#include "config.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

#include "cJSON.h"

config config::load_config_from_file(const std::string& filepath)
{
    config cfg;
    std::ifstream file(filepath);

    if (!file.is_open())
    {
        std::cerr << "Error opening config file: " << filepath << "\n";
        return cfg;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();
    file.close();

    cJSON* json = cJSON_Parse(json_str.c_str());
    if (!json)
    {
        std::cerr << "Error parsing JSON: " << cJSON_GetErrorPtr() << "\n";
        return cfg;
    }

    cJSON* ip_v4 = cJSON_GetObjectItemCaseSensitive(json, "ip_v4");
    cJSON* ip_v6 = cJSON_GetObjectItemCaseSensitive(json, "ip_v6");
    cJSON* port = cJSON_GetObjectItemCaseSensitive(json, "port");
    cJSON* ack_timeout = cJSON_GetObjectItemCaseSensitive(json, "ack_timeout");
    cJSON* max_auth_attempts = cJSON_GetObjectItemCaseSensitive(json, "max_auth_attempts");
    cJSON* max_auth_attempts_map_size = cJSON_GetObjectItemCaseSensitive(json, "max_auth_attempts_map_size");

    if (cJSON_IsString(ip_v4) && cJSON_IsString(ip_v6) && cJSON_IsNumber(port) && cJSON_IsNumber(ack_timeout) &&
        cJSON_IsNumber(max_auth_attempts) && cJSON_IsNumber(max_auth_attempts_map_size))
    {
        cfg.ip_v4 = ip_v4->valuestring;
        cfg.ip_v6 = ip_v6->valuestring;
        cfg.port = port->valueint;
        cfg.ack_timeout = ack_timeout->valueint;
        cfg.max_auth_attempts = max_auth_attempts->valueint;
        cfg.max_auth_attempts_map_size = max_auth_attempts_map_size->valueint;
    }
    else
    {
        std::cerr << "Invalid JSON format.\n";
    }
    cJSON_Delete(json);
    return cfg;
}
