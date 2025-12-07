#include "config.hpp"

#include <cJSON.h>
#include <fstream>
#include <stdexcept>

namespace config
{

std::string get_env_var(const char* env_var, const char* default_value)
{
    const char* value = std::getenv(env_var);
    return value ? std::string(value) : std::string(default_value);
}

void load_config_from_file(const std::string& config_path, server_config& config)
{
    std::ifstream file(get_env_var("CONFIG_PATH", config_path.c_str()));
    if (!file.is_open())
    {
        throw std::runtime_error("Cannot open config file: " + config_path);
    }

    std::string json_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    cJSON* json = cJSON_Parse(json_content.c_str());
    if (!json)
    {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != nullptr)
        {
            throw std::runtime_error("Failed to parse config file: " + std::string(error_ptr));
        }
        throw std::runtime_error("Failed to parse config file: unknown error");
    }

    // Validate and parse ip_v4
    cJSON* ip_v4 = cJSON_GetObjectItemCaseSensitive(json, "ip_v4");
    if (!ip_v4 || !cJSON_IsString(ip_v4))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ip_v4' field in config file");
    }
    config.ip_v4 = ip_v4->valuestring;

    // Validate and parse ip_v6
    cJSON* ip_v6 = cJSON_GetObjectItemCaseSensitive(json, "ip_v6");
    if (!ip_v6 || !cJSON_IsString(ip_v6))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ip_v6' field in config file");
    }
    config.ip_v6 = ip_v6->valuestring;

    // Validate and parse network_port
    cJSON* network_port = cJSON_GetObjectItemCaseSensitive(json, "network_port");
    if (!network_port || !cJSON_IsNumber(network_port))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'network_port' field in config file");
    }
    config.network_port = static_cast<std::uint16_t>(network_port->valueint);

    // Validate and parse ack_timeout
    cJSON* ack_timeout = cJSON_GetObjectItemCaseSensitive(json, "ack_timeout");
    if (!ack_timeout || !cJSON_IsNumber(ack_timeout))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ack_timeout' field in config file");
    }
    config.ack_timeout = static_cast<std::uint32_t>(ack_timeout->valueint);

    // Validate and parse max_auth_attempts
    cJSON* max_auth_attempts = cJSON_GetObjectItemCaseSensitive(json, "max_auth_attempts");
    if (!max_auth_attempts || !cJSON_IsNumber(max_auth_attempts))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'max_auth_attempts' field in config file");
    }
    config.max_auth_attempts = static_cast<std::uint32_t>(max_auth_attempts->valueint);

    cJSON_Delete(json);
}
} // namespace config
