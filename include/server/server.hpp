#ifndef SERVER_HPP
#define SERVER_HPP

#include <asio.hpp>

const int PORT = 9999;
const size_t BUFFER_SIZE = 1024;

class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    explicit tcp_session(asio::ip::tcp::socket socket);
    void start();

  private:
    void do_read();
    void do_write(size_t length);

    asio::ip::tcp::socket m_socket;
    std::array<char, BUFFER_SIZE> m_data;
};

class udp_server
{
  public:
    udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint);

  private:
    void do_receive();           // Recepción asíncrona
    void do_send(size_t length); // Envío asíncrono

    asio::ip::udp::socket m_socket;            // Socket UDP
    asio::ip::udp::endpoint m_sender_endpoint; // Dirección del cliente
    std::array<char, BUFFER_SIZE> m_data;      // Buffer de datos
};

class server
{
  public:
    ~server()
    {
        m_tcp4_acceptor.close();
        m_tcp6_acceptor.close();
    }
    explicit server(asio::io_context& io_context);

  private:
    void do_accept();

    asio::io_context& m_io_context;
    asio::ip::tcp::acceptor m_tcp4_acceptor;
    asio::ip::tcp::acceptor m_tcp6_acceptor;
    udp_server m_udp_server_ipv4;
    udp_server m_udp_server_ipv6;
};

#endif // SERVER_HPP
