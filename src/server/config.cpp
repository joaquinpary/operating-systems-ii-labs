#include "config.hpp"

#include <cJSON.h>
#include <cstdlib>
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

    cJSON* ip_v4 = cJSON_GetObjectItemCaseSensitive(json, "ip_v4");
    if (!ip_v4 || !cJSON_IsString(ip_v4))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ip_v4' field in config file");
    }
    config.ip_v4 = ip_v4->valuestring;

    cJSON* ip_v6 = cJSON_GetObjectItemCaseSensitive(json, "ip_v6");
    if (!ip_v6 || !cJSON_IsString(ip_v6))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ip_v6' field in config file");
    }
    config.ip_v6 = ip_v6->valuestring;

    cJSON* network_port = cJSON_GetObjectItemCaseSensitive(json, "network_port");
    if (!network_port || !cJSON_IsNumber(network_port))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'network_port' field in config file");
    }
    config.network_port = static_cast<std::uint16_t>(network_port->valueint);

    cJSON* ack_timeout = cJSON_GetObjectItemCaseSensitive(json, "ack_timeout");
    if (!ack_timeout || !cJSON_IsNumber(ack_timeout))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'ack_timeout' field in config file");
    }
    config.ack_timeout = static_cast<std::uint32_t>(ack_timeout->valueint);

    cJSON* max_auth_attempts = cJSON_GetObjectItemCaseSensitive(json, "max_auth_attempts");
    if (!max_auth_attempts || !cJSON_IsNumber(max_auth_attempts))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'max_auth_attempts' field in config file");
    }
    config.max_auth_attempts = static_cast<std::uint32_t>(max_auth_attempts->valueint);

    cJSON* max_retries = cJSON_GetObjectItemCaseSensitive(json, "max_retries");
    if (!max_retries || !cJSON_IsNumber(max_retries))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'max_retries' field in config file");
    }
    config.max_retries = static_cast<std::uint32_t>(max_retries->valueint);

    cJSON* keepalive_timeout = cJSON_GetObjectItemCaseSensitive(json, "keepalive_timeout");
    if (!keepalive_timeout || !cJSON_IsNumber(keepalive_timeout))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'keepalive_timeout' field in config file");
    }
    config.keepalive_timeout = static_cast<std::uint32_t>(keepalive_timeout->valueint);

    cJSON* pool_size = cJSON_GetObjectItemCaseSensitive(json, "pool_size");
    if (!pool_size || !cJSON_IsNumber(pool_size))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'pool_size' field in config file");
    }
    config.pool_size = static_cast<std::uint32_t>(pool_size->valueint);

    cJSON* worker_threads = cJSON_GetObjectItemCaseSensitive(json, "worker_threads");
    if (!worker_threads || !cJSON_IsNumber(worker_threads))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'worker_threads' field in config file");
    }
    config.worker_threads = static_cast<std::uint32_t>(worker_threads->valueint);

    cJSON* credentials_path = cJSON_GetObjectItemCaseSensitive(json, "credentials_path");
    if (!credentials_path || !cJSON_IsString(credentials_path))
    {
        cJSON_Delete(json);
        throw std::runtime_error("Missing or invalid 'credentials_path' field in config file");
    }
    config.credentials_path = credentials_path->valuestring;

    cJSON_Delete(json);
}
} // namespace config
