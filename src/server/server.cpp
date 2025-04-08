#include "server.hpp"
#include "server/database.hpp"
#include <iostream>
#include <pqxx/pqxx>

// Implementación de session
tcp_session::tcp_session(asio::ip::tcp::socket socket) : m_socket(std::move(socket))
{
}

void tcp_session::start()
{
    do_read();
}

void tcp_session::do_read()
{
    auto self(shared_from_this());
    m_socket.async_read_some(asio::buffer(m_data), [this, self](asio::error_code ec, size_t length) {
        if (!ec)
        {
            std::cout << "Datos recibidos: " << std::string(m_data.data(), length) << "\n";

            do_write(length);

            do_read();
        }
    });
}

void tcp_session::do_write(size_t length)
{
    auto self(shared_from_this());
    asio::async_write(m_socket, asio::buffer(m_data, length), [this, self](asio::error_code ec, size_t) {
        if (ec)
        {
            std::cerr << "Error al enviar datos: " << ec.message() << "\n";
        }
    });
}

// Implementación de udp_server
udp_server::udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint)
    : m_socket(io_context, endpoint)
{
    do_receive();
}

void udp_server::do_receive()
{
    m_socket.async_receive_from(asio::buffer(m_data), m_sender_endpoint, [this](auto ec, size_t length) {
        if (!ec)
        {
            std::cout << "Datos UDP recibidos: " << std::string(m_data.data(), length) << "\n";
            do_send(length);
        }
        do_receive();
    });
}

void udp_server::do_send(size_t length)
{
    m_socket.async_send_to(asio::buffer(m_data, length), m_sender_endpoint, [this](auto ec, size_t length) {
        if (ec)
        {
            std::cerr << "Error al enviar datos UDP: " << ec.message() << "\n";
        }
    });
}

// Implementación de server
server::server(asio::io_context& io_context)
    : m_io_context(io_context),
      m_tcp4_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 9999)),
      m_tcp6_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::make_address("::1"), 9999)),
      m_udp_server_ipv4(io_context, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 9999)),
      m_udp_server_ipv6(io_context, asio::ip::udp::endpoint(asio::ip::make_address("::1"), 9999))

{
    m_tcp4_acceptor.set_option(asio::socket_base::reuse_address(true));
    m_tcp4_acceptor.listen();
    m_tcp6_acceptor.set_option(asio::socket_base::reuse_address(true));
    m_tcp6_acceptor.listen();

    do_accept();
}

void server::do_accept()
{
    m_tcp4_acceptor.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec)
        {
            std::cout << "Nueva conexión desde: " << socket.remote_endpoint() << "\n";
            std::make_shared<tcp_session>(std::move(socket))->start();
        }
        else
        {
            std::cerr << "Error en accept: " << ec.message() << "\n";
        }
        do_accept();
    });

    m_tcp6_acceptor.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec)
        {
            std::cout << "Nueva conexión desde: " << socket.remote_endpoint() << "\n";
            std::make_shared<tcp_session>(std::move(socket))->start();
        }
        else
        {
            std::cerr << "Error en accept: " << ec.message() << "\n";
        }
        do_accept();
    });
}

int example_database()
{
    auto conn = connect_to_database();
    if (!conn)
    {
        std::cerr << "Failed to connect to the database." << std::endl;
        return 1;
    }
    try
    {
        pqxx::work txn(*conn);

        create_table(txn);

        insert_database(txn, 2, "Test Event", "Test Origin", "Test Level");
        txn.commit();

        conn->disconnect();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}

