#ifndef SERVER_HPP
#define SERVER_HPP

#include "auth_module.hpp"
#include "config.hpp"
#include "message_handler.hpp"
#include "session_manager.hpp"
#include <array>
#include <asio.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace server_constants
{
inline constexpr std::uint16_t DEFAULT_PORT = 9999;
inline constexpr std::size_t DEFAULT_BUFFER_SIZE = 1024;
inline constexpr const char* DEFAULT_IPV4_ADDRESS = "127.0.0.1";
inline constexpr const char* DEFAULT_IPV6_ADDRESS = "::1";
} // namespace server_constants

config::server_config make_default_server_config(); // revisar, no deberiamos tener configuracion default

class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    tcp_session(asio::ip::tcp::socket socket, message_handler& msg_handler, session_manager& session_mgr);
    void start();

  private:
    void do_read();
    void do_write(const std::string& data);
    void process_received_data(std::size_t bytes_transferred);

    asio::ip::tcp::socket m_socket;
    std::array<char, server_constants::DEFAULT_BUFFER_SIZE> m_data;
    message_handler& m_message_handler;
    session_manager& m_session_manager;
    std::string m_session_id;
};

class udp_server
{
  public:
    udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint, message_handler& msg_handler,
               session_manager& session_mgr);

  private:
    void do_receive();
    void do_send(const std::string& data, const asio::ip::udp::endpoint& target_endpoint);
    void process_received_data(const std::string& json_input, const asio::ip::udp::endpoint& sender_endpoint);
    std::string get_session_id_from_endpoint(const asio::ip::udp::endpoint& endpoint) const;

    asio::ip::udp::socket m_socket;
    asio::ip::udp::endpoint m_sender_endpoint;
    std::array<char, server_constants::DEFAULT_BUFFER_SIZE> m_data;
    message_handler& m_message_handler;
    session_manager& m_session_manager;
};

class server
{
  public:
    server(asio::io_context& io_context, const config::server_config& config,
           std::unique_ptr<session_manager> session_mgr, std::unique_ptr<auth_module> auth_mod,
           std::unique_ptr<message_handler> msg_handler);
    ~server();
    void start();
    void stop();

  private:
    void start_accept(asio::ip::tcp::acceptor& acceptor);
    static void configure_acceptor(asio::ip::tcp::acceptor& acceptor, const asio::ip::tcp::endpoint& endpoint);
    asio::ip::tcp::endpoint make_tcp_endpoint(const std::string& address, std::uint16_t port) const;
    asio::ip::udp::endpoint make_udp_endpoint(const std::string& address, std::uint16_t port) const;
    static asio::ip::address parse_address(const std::string& address_literal);

    asio::io_context& m_io_context;
    config::server_config m_config;
    asio::ip::tcp::acceptor m_tcp4_acceptor;
    asio::ip::tcp::acceptor m_tcp6_acceptor;
    std::unique_ptr<udp_server> m_udp_server_ipv4;
    std::unique_ptr<udp_server> m_udp_server_ipv6;
    std::unique_ptr<session_manager> m_session_manager;
    std::unique_ptr<auth_module> m_auth_module;
    std::unique_ptr<message_handler> m_message_handler;
};

#endif // SERVER_HPP
