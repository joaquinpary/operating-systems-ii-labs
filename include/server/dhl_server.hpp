#ifndef DHL_SERVER_HPP
#define DHL_SERVER_HPP

#include "database.hpp"
#include <asio.hpp>
#include "config.hpp"
#include <chrono>
#include <queue>
#include <string>
#include <unordered_map>

#define PATH_CONFIG "config/server_parameters.json"

const size_t BUFFER_SIZE = 1024;

struct last_ongoing_message
{
    std::string data;
    std::chrono::steady_clock::time_point timestamp;
    asio::steady_timer ack_timer;
    bool waiting_ack = false;

  udp_client(asio::io_context& io_context)
    : udp_last_ongoing_message(std::make_unique<last_ongoing_message>(io_context)) {}
};

class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    explicit tcp_session(asio::ip::tcp::socket socket, database_manager& db_manager, config& config_params);
    void start();

  private:
    void do_auth();
    void do_read();
    void do_write_ack(const std::string& message);
    void close_connection(const std::string& reason);
    void start_ack_timer();

    config m_config_params;

    asio::ip::tcp::socket m_socket;
    std::array<char, BUFFER_SIZE> m_data;
    int m_auth_attempts = 0;

    std::string m_client_id;
    std::string m_client_type;
    last_ongoing_message m_last_ongoing_message;

    database_manager& m_database_manager;
};

class udp_server
{
  public:

    explicit udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint, database_manager& db_manager, config& config_params);

  private:
    void do_receive(); // Recepción asíncrona
    void do_send(const std::string& message, const asio::ip::udp::endpoint& endpoint, const std::string& username);
    void do_auth_udp(const std::string& msg, asio::ip::udp::endpoint sender_endpoint);
    bool register_auth_attempt(const std::string& username);
    void start_udp_ack_timer(const std::string& username);
    bool check_auth(const std::string& username);

    config m_config_params;

    asio::ip::udp::socket m_socket;            // Socket UDP
    asio::ip::udp::endpoint m_sender_endpoint; // Dirección del cliente
    std::array<char, BUFFER_SIZE> m_data;      // Buffer de datos


    std::unordered_map <std::string, std::unique_ptr<udp_client>> m_client_map;
    std::unordered_map <std::string, int> m_auth_attempts_map;
    std::queue <std::string> m_auth_attempts_fifo;

    database_manager& m_database_manager; // Referencia a la base de datos
};

class server
{
  public:
    ~server()
    {
        m_tcp4_acceptor.close();
        m_tcp6_acceptor.close(); // udp?
    }
    explicit server(asio::io_context& io_context, config& config_params);

  private:
    void do_accept();

    asio::io_context& m_io_context;
    config m_config_params;
    asio::ip::tcp::acceptor m_tcp4_acceptor;
    asio::ip::tcp::acceptor m_tcp6_acceptor;
    udp_server m_udp4_server;
    udp_server m_udp6_server;

    database_manager m_database_manager;
};

#endif // SERVER_HPP
