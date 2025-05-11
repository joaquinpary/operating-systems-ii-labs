#ifndef DHL_SERVER_HPP
#define DHL_SERVER_HPP

#include "config.hpp"
#include "database.hpp"
#include <asio.hpp>
#include <chrono>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

#ifndef TESTING
#define PATH_CONFIG "/etc/dhl_server/server_parameters.json"
#else
#define PATH_CONFIG "config/server_parameters.json"
#endif

const size_t BUFFER_SIZE = 1024;
const int KEEPALIVE_TIMEOUT = 60; // seconds

class tcp_session;
class udp_server;

class server
{
  public:
    ~server()
    {
        m_tcp4_acceptor.close();
        m_tcp6_acceptor.close();
    }
    explicit server(asio::io_context& io_context, config& config_params);
    void register_tcp_client(const std::string& username, std::shared_ptr<tcp_session> client);
    void unregister_tcp_client(const std::string& username);
    void register_udp_client(const std::string& username, const asio::ip::udp::endpoint& endpoint);
    void unregister_udp_client(const std::string& username);

    bool is_client_active_tcp(const std::string& username);
    bool is_client_active_udp(const std::string& username);

    std::shared_ptr<tcp_session> get_tcp_session(const std::string& username);
    asio::ip::udp::endpoint get_udp_endpoint(const std::string& username);
    udp_server* get_udp_server(asio::ip::udp::endpoint endpoint);

  private:
    void do_accept();

    asio::io_context& m_io_context;
    config m_config_params;
    asio::ip::tcp::acceptor m_tcp4_acceptor;
    asio::ip::tcp::acceptor m_tcp6_acceptor;
    std::unique_ptr<udp_server> m_udp4_server;
    std::unique_ptr<udp_server> m_udp6_server;
        
    std::unordered_map<std::string, std::shared_ptr<tcp_session>> m_active_tcp_clients;
    std::unordered_map<std::string, asio::ip::udp::endpoint> m_active_udp_clients;

    database_manager m_database_manager;
};

struct last_ongoing_message
{
    std::string data;
    std::chrono::steady_clock::time_point timestamp;
    asio::steady_timer ack_timer;
    bool waiting_ack = false;

    last_ongoing_message(asio::io_context& io_context) : ack_timer(io_context)
    {
    }
};

struct udp_client
{
    bool authenticated = false;
    std::string username;
    std::string client_type;
    asio::ip::udp::endpoint endpoint;
    std::unique_ptr<last_ongoing_message> udp_last_ongoing_message;
    asio::steady_timer keepalive_timer;
    const::std::chrono::seconds keepalive_timeout = std::chrono::seconds(KEEPALIVE_TIMEOUT);

    udp_client(asio::io_context& io_context)
        : keepalive_timer(io_context),
          udp_last_ongoing_message(std::make_unique<last_ongoing_message>(io_context))
    {
    }
};

class udp_server
{
  public:
    explicit udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint,
                        database_manager& db_manager, server& m_main_server, config& config_params);
    void send_msg(const std::string& message, const asio::ip::udp::endpoint& endpoint, const std::string& username);

  private:
    void do_receive(); // Recepción asíncrona
    void do_send(const std::string& message, const asio::ip::udp::endpoint& endpoint, const std::string& username);
    void do_auth_udp(const std::string& msg, asio::ip::udp::endpoint sender_endpoint);
    bool register_auth_attempt(const std::string& username);
    void start_udp_ack_timer(const std::string& username);
    bool check_auth(const std::string& username);
    bool handle_udp_message(const std::string& message, const asio::ip::udp::endpoint& sender_endpoint);
    void start_keepalive_timer(const std::string& username);
    void reset_keepalive_timer(const std::string& username);

    server& m_server;

    config m_config_params;

    asio::ip::udp::socket m_socket;            // Socket UDP
    asio::ip::udp::endpoint m_sender_endpoint; // Dirección del cliente
    std::array<char, BUFFER_SIZE> m_data;      // Buffer de datos

    std::unordered_map<std::string, std::unique_ptr<udp_client>> m_client_map; // (username, client)
    std::unordered_map<std::string, int> m_auth_attempts_map; // (username, attempts)
    std::queue<std::string> m_auth_attempts_fifo; // FIFO queue for auth attempts

    database_manager& m_database_manager; // Referencia a la base de datos
};

class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    explicit tcp_session(asio::ip::tcp::socket socket, database_manager& db_manager, server& main_server, config& config_params);
    void start();
    std::string get_username();
    std::string get_client_type();
    void send_msg(const std::string& message);

  private:
    void do_auth();
    void do_read();
    void do_write(const std::string& message);
    void close_connection(const std::string& reason);
    void start_ack_timer();
    void handle_tcp_message(const std::string& msg);
    void start_keepalive_timer();
    void reset_keepalive_timer();
    
    server& m_server;

    config m_config_params;

    asio::ip::tcp::socket m_socket;
    std::array<char, BUFFER_SIZE> m_data;
    int m_auth_attempts = 0;

    std::string m_username;
    std::string m_client_type;
    last_ongoing_message m_last_ongoing_message;

    asio::steady_timer m_keepalive_timer;
    const std::chrono::seconds m_keepalive_timeout = std::chrono::seconds(KEEPALIVE_TIMEOUT);

    database_manager& m_database_manager;
};

#endif // SERVER_HPP
