#include "server.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

server_config make_default_server_config()
{
    return server_config{
        .tcp = server_endpoint_config{
            .address_v4 = server_constants::DEFAULT_IPV4_ADDRESS,
            .address_v6 = server_constants::DEFAULT_IPV6_ADDRESS,
            .port = server_constants::DEFAULT_PORT,
        },
        .udp = server_endpoint_config{
            .address_v4 = server_constants::DEFAULT_IPV4_ADDRESS,
            .address_v6 = server_constants::DEFAULT_IPV6_ADDRESS,
            .port = server_constants::DEFAULT_PORT,
        },
    };
}

asio::ip::address server::parse_address(const std::string& address_literal)
{
    try
    {
        return asio::ip::make_address(address_literal);
    }
    catch (const std::exception& ex)
    {
        throw std::runtime_error("Failed to parse address '" + address_literal + "': " + ex.what());
    }
}

tcp_session::tcp_session(asio::ip::tcp::socket socket) : m_socket(std::move(socket))
{
}

void tcp_session::start()
{
    do_read();
}

void tcp_session::do_read()
{
    auto self = shared_from_this();
    m_socket.async_read_some(asio::buffer(m_data),
                             [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
                                 if (!ec)
                                 {
                                     std::cout << "TCP: Received " << bytes_transferred << " bytes\n";
                                     do_write(bytes_transferred);
                                     return;
                                 }
                                 if (ec != asio::error::operation_aborted)
                                 {
                                     std::cerr << "TCP read error: " << ec.message() << '\n';
                                 }
                             });
}

void tcp_session::do_write(std::size_t length)
{
    auto self = shared_from_this();
    asio::async_write(m_socket, asio::buffer(m_data, length),
                      [this, self](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
                          if (!ec)
                          {
                              do_read();
                              return;
                          }
                          if (ec != asio::error::operation_aborted)
                          {
                              std::cerr << "TCP write error: " << ec.message() << '\n';
                          }
                      });
}

udp_server::udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint)
    : m_socket(io_context)
{
    m_socket.open(endpoint.protocol());
    m_socket.set_option(asio::socket_base::reuse_address(true));
    m_socket.bind(endpoint);
    do_receive();
}

void udp_server::do_receive()
{
    m_socket.async_receive_from(asio::buffer(m_data), m_sender_endpoint,
                                [this](const asio::error_code& ec, std::size_t bytes_transferred) {
                                    if (!ec)
                                    {
                                        std::cout << "UDP: Received " << bytes_transferred << " bytes from "
                                                  << m_sender_endpoint << '\n';
                                        do_send(bytes_transferred);
                                        return;
                                    }
                                    if (ec != asio::error::operation_aborted)
                                    {
                                        std::cerr << "UDP receive error: " << ec.message() << '\n';
                                        do_receive();
                                    }
                                });
}

void udp_server::do_send(std::size_t length)
{
    m_socket.async_send_to(asio::buffer(m_data, length), m_sender_endpoint,
                           [this](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
                               if (ec && ec != asio::error::operation_aborted)
                               {
                                   std::cerr << "UDP send error: " << ec.message() << '\n';
                               }
                               do_receive();
                           });
}

server::server(asio::io_context& io_context, const server_config& config)
    : m_io_context(io_context),
      m_config(config),
      m_tcp4_acceptor(io_context),
      m_tcp6_acceptor(io_context),
      m_udp_server_ipv4(std::make_unique<udp_server>(io_context, make_udp_endpoint(m_config.udp.address_v4, m_config.udp.port))),
      m_udp_server_ipv6(std::make_unique<udp_server>(io_context, make_udp_endpoint(m_config.udp.address_v6, m_config.udp.port)))
{
    configure_acceptor(m_tcp4_acceptor, make_tcp_endpoint(m_config.tcp.address_v4, m_config.tcp.port));
    configure_acceptor(m_tcp6_acceptor, make_tcp_endpoint(m_config.tcp.address_v6, m_config.tcp.port));
}

server::~server()
{
    stop();
}

void server::start()
{
    start_accept(m_tcp4_acceptor);
    start_accept(m_tcp6_acceptor);
    std::cout << "Server started:\n"
              << "  TCP IPv4: " << m_config.tcp.address_v4 << ":" << m_config.tcp.port << '\n'
              << "  TCP IPv6: " << m_config.tcp.address_v6 << ":" << m_config.tcp.port << '\n'
              << "  UDP IPv4: " << m_config.udp.address_v4 << ":" << m_config.udp.port << '\n'
              << "  UDP IPv6: " << m_config.udp.address_v6 << ":" << m_config.udp.port << '\n';
}

void server::stop()
{
    if (m_tcp4_acceptor.is_open())
    {
        m_tcp4_acceptor.close();
    }
    if (m_tcp6_acceptor.is_open())
    {
        m_tcp6_acceptor.close();
    }
}

void server::start_accept(asio::ip::tcp::acceptor& acceptor)
{
    acceptor.async_accept([this, &acceptor](const asio::error_code& ec, asio::ip::tcp::socket socket) {
        if (!ec)
        {
            std::cout << "Accepted TCP connection from " << socket.remote_endpoint() << '\n';
            std::make_shared<tcp_session>(std::move(socket))->start();
        }
        else if (ec != asio::error::operation_aborted)
        {
            std::cerr << "Accept error: " << ec.message() << '\n';
        }

        if (acceptor.is_open())
        {
            start_accept(acceptor);
        }
    });
}

void server::configure_acceptor(asio::ip::tcp::acceptor& acceptor, const asio::ip::tcp::endpoint& endpoint)
{
    acceptor.open(endpoint.protocol());
    acceptor.set_option(asio::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();
}

asio::ip::tcp::endpoint server::make_tcp_endpoint(const std::string& address, std::uint16_t port) const
{
    return asio::ip::tcp::endpoint(parse_address(address), port);
}

asio::ip::udp::endpoint server::make_udp_endpoint(const std::string& address, std::uint16_t port) const
{
    return asio::ip::udp::endpoint(parse_address(address), port);
}
