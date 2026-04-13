#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>

namespace config
{
/** Runtime configuration loaded by the server process at startup. */
struct server_config
{
    std::string ip_v4;               ///< IPv4 bind address.
    std::string ip_v6;               ///< IPv6 bind address.
    std::uint16_t network_port;      ///< Shared TCP/UDP listening port.
    std::uint16_t api_rest_port;     ///< REST API listening port.
    std::uint32_t ack_timeout;       ///< ACK timeout in milliseconds.
    std::uint32_t max_auth_attempts; ///< Max invalid auth attempts before blacklisting.
    std::uint32_t max_retries;       ///< Max ACK retries before disconnecting the client.
    std::uint32_t keepalive_timeout; ///< Keepalive timeout in seconds.
    std::uint32_t pool_size;         ///< Database connection pool size.
    std::uint32_t worker_threads;    ///< Worker thread count.
    std::string credentials_path;    ///< Directory containing client credential files.
    std::string mqtt_broker_host;    ///< MQTT broker hostname.
    std::uint16_t mqtt_broker_port;  ///< MQTT broker port.
};

/**
 * Read an environment variable or fall back to a default value.
 * @param env_var Environment variable name.
 * @param default_value Fallback value when the variable is unset.
 * @return The resolved string value.
 */
std::string get_env_var(const char* env_var, const char* default_value);

/**
 * Load and validate server configuration from disk.
 * @param config_path Path to the JSON configuration file.
 * @param config Output structure populated on success.
 * @throws std::runtime_error If the file is missing, invalid or incomplete.
 */
void load_config_from_file(const std::string& config_path, server_config& config);
} // namespace config

#endif // CONFIG_HPP
