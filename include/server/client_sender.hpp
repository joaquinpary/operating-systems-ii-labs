#pragma once

#include <asio/ip/udp.hpp>
#include <memory>
#include <string>

class tcp_session; // forward declaration
struct udp_client;
class udp_server;
class server;

struct client_sender
{
    virtual ~client_sender() = default;
    virtual void send(const std::string& msg) = 0;
};

struct tcp_client_sender : client_sender
{
    std::shared_ptr<tcp_session> m_tcp_session;
    tcp_client_sender(std::shared_ptr<tcp_session> session); // <--- solo la firma
    void send(const std::string& msg) override;
};

struct udp_client_sender : client_sender
{
    asio::ip::udp::endpoint m_udp_endpoint;
    std::string m_username;
    udp_server* m_udp_server;
    udp_client_sender(asio::ip::udp::endpoint endpoint, std::string username,
                      udp_server* udp_server); // <--- solo la firma
    void send(const std::string& msg) override;
};

std::shared_ptr<client_sender> get_client_sender(const std::string& username, server& main_server);
