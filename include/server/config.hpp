#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

struct config
{
    std::string ip_v4;
    std::string ip_v6;
    int port_tcp_v4;
    int port_tcp_v6;
    int port_udp_v4;
    int port_udp_v6;
    int ack_timeout;
    int max_auth_attempts;
    int max_auth_attempts_map_size;

    static config load_config_from_file(const std::string& filepath);
};

#endif
