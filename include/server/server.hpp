#ifndef SERVER_HPP
#define SERVER_HPP

#include <asio.hpp>
#include <array>
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

struct server_endpoint_config
{
    std::string address_v4;
    std::string address_v6;
    std::uint16_t port;
};

struct server_config
{
    server_endpoint_config tcp;
    server_endpoint_config udp;
};

server_config make_default_server_config();

class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    explicit tcp_session(asio::ip::tcp::socket socket);
    void start();

  private:
    void do_read();
    void do_write(std::size_t length);

    asio::ip::tcp::socket m_socket;
    std::array<char, server_constants::DEFAULT_BUFFER_SIZE> m_data;
};

class udp_server
{
  public:
    udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint);

  private:
    void do_receive();
    void do_send(std::size_t length);

    asio::ip::udp::socket m_socket;
    asio::ip::udp::endpoint m_sender_endpoint;
    std::array<char, server_constants::DEFAULT_BUFFER_SIZE> m_data;
};

class server
{
  public:
    explicit server(asio::io_context& io_context, const server_config& config = make_default_server_config());
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
    server_config m_config;
    asio::ip::tcp::acceptor m_tcp4_acceptor;
    asio::ip::tcp::acceptor m_tcp6_acceptor;
    std::unique_ptr<udp_server> m_udp_server_ipv4;
    std::unique_ptr<udp_server> m_udp_server_ipv6;
};

#endif // SERVER_HPP
