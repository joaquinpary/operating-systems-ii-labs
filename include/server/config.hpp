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
};

// Get environment variable or return default value
std::string get_env_var(const char* env_var, const char* default_value);

void load_config_from_file(const std::string& config_path, server_config& config);
} // namespace config

#endif // CONFIG_HPP
