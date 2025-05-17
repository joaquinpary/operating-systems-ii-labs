#include "client_sender.hpp"
#include "dhl_server.hpp" // para acceder a udp_server::do_send()

#include <iostream>

#include "client_sender.hpp"

tcp_client_sender::tcp_client_sender(std::shared_ptr<tcp_session> session) : m_tcp_session(std::move(session))
{
}

udp_client_sender::udp_client_sender(asio::ip::udp::endpoint endpoint, std::string username, udp_server* udp_server)
    : m_udp_endpoint(endpoint), m_username(username), m_udp_server(udp_server)
{
}

// TCP sender
void tcp_client_sender::send(const std::string& msg)
{
    std::cout << "Sending message in sender tcp: " << msg << "\n" << std::flush;
    if (m_tcp_session)
    {
        std::cout << "Sending message to TCP session: " << m_tcp_session->get_username() << "\n" << std::flush;
        m_tcp_session->send_msg(msg); // Debe ser un método tuyo que envía texto
    }
    else
    {
        std::cerr << "TCP session not available or closed.\n";
    }
}

// UDP sender
void udp_client_sender::send(const std::string& msg)
{
    std::cout << "Sending message in sender udp: " << msg << "\n" << std::flush;
    if (m_udp_endpoint != asio::ip::udp::endpoint() && m_udp_server)
    {
        std::cout << "Sending message to UDP endpoint: " << m_udp_endpoint.address().to_string() << ":"
                  << m_udp_endpoint.port() << "\n"
                  << std::flush;
        m_udp_server->send_msg(msg, m_udp_endpoint, m_username);
    }
    else
    {
        std::cerr << "UDP client or server invalid.\n";
    }
}

std::shared_ptr<client_sender> get_client_sender(const std::string& username, server& main_server)
{
    if (auto tcp = main_server.get_tcp_session(username))
    {
        return std::make_shared<tcp_client_sender>(tcp);
    }
    auto udp_endpoint = main_server.get_udp_endpoint(username);
    if (udp_endpoint != asio::ip::udp::endpoint())
    {
        return std::make_shared<udp_client_sender>(udp_endpoint, username, main_server.get_udp_server(udp_endpoint));
    }
    return nullptr;
}
