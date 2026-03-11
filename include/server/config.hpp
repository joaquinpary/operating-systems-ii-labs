#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>

namespace config
{
struct server_config
{
    std::string ip_v4;
    std::string ip_v6;
    std::uint16_t network_port;
    std::uint32_t ack_timeout;
    std::uint32_t max_auth_attempts;
    std::uint32_t max_retries;
    std::uint32_t keepalive_timeout;
    std::uint32_t pool_size;
    std::uint32_t worker_threads;
    std::string credentials_path;
};

std::string get_env_var(const char* env_var, const char* default_value);

void load_config_from_file(const std::string& config_path, server_config& config);
} // namespace config

#endif // CONFIG_HPP
