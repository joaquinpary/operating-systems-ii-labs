#include "dhl_server.hpp"
#include "database.hpp"
#include "json_manager.h"
#include "logger.h"
#include "utilities.hpp"
#include <iostream>
#include <pqxx/pqxx>

// TCP Session implementation
tcp_session::tcp_session(asio::ip::tcp::socket socket, database_manager& db, config& config_params)
    : m_socket(std::move(socket)), m_config_params(config_params), m_database_manager(db),
      m_last_ongoing_message(static_cast<asio::io_context&>(m_socket.get_executor().context()))
{
}

void tcp_session::start()
{
    do_auth();
}

void tcp_session::do_auth()
{
    auto self = shared_from_this();

    m_socket.async_read_some(asio::buffer(m_data), [this, self](asio::error_code ec, size_t length) {
        if (ec)
        {
            close_connection("Read error during auth: " + ec.message());
            return;
        }

        client_auth_request req = deserialize_client_auth_request(m_data.data());

        bool valid_credentials = m_database_manager.authenticate_client(req.payload.username, req.payload.password);
        std::string json = build_auth_response_json(valid_credentials, valid_credentials ? "Logged in successfully"
                                                                                         : "Invalid credentials");
        copy_response_to_buffer(json, m_data);

        if (m_auth_attempts > m_config_params.max_auth_attempts)
        {
            close_connection("Max authentication attempts reached.");
            return;
        }

        asio::async_write(m_socket, asio::buffer(m_data, json.size()),
                          [this, self, length](asio::error_code ec, size_t) {
                              if (ec)
                              {
                                  std::cerr << "Failed to send the data: " << ec.message() << "\n";
                              }
                          });

        m_auth_attempts++;

        if (valid_credentials)
        {
            do_read();
        }
        else
        {
            do_auth();
        }
    });
}

void tcp_session::handle_tcp_message(const std::string& message)
{

    char* type = get_type(message.c_str());
    if (!type)
    {
        free(type);
        return;
    }

    int type_code = get_message_code(type);
    free(type);

    switch (type_code)
    {
    case CLIENT_KEEPALIVE: {
        // Handle keepalive message
        break;
    }
    case CLIENT_INVENTORY_UPDATE:
        // Handle inventory update message
        break;
    case CLIENT_ACKNOWLEDGMENT: {
        client_acknowledgment acknowledgment = deserialize_client_acknowledgment(m_data.data());
        // Process acknowledgment message
        client_acknowledgment ack = deserialize_client_acknowledgment(m_data.data());
        m_last_ongoing_message.waiting_ack = false;
        m_last_ongoing_message.data.clear();
        m_last_ongoing_message.ack_timer.cancel();
        break;
    }
    case CLIENT_INFECTION_ALERT:
        // Handle infection alert message
        break;
    case WAREHOUSE_SEND_STOCK_TO_HUB:
        // Handle send stock to hub message
        break;
    case WAREHOUSE_REQUEST_STOCK:
        // Handle request stock message
        break;
    case HUB_REQUEST_STOCK:
        // Handle hub request stock message
        break;
    default:
        std::cerr << "Unknown TCP message type: " << type_code << "\n";
        break;
    }
}

void tcp_session::do_read()
{
    auto self(shared_from_this());
    m_socket.async_read_some(asio::buffer(m_data), [this, self](asio::error_code ec, size_t length) {
        if (!ec)
        {
            std::string received_data_str(m_data.data(), length);
            handle_tcp_message(received_data_str);

            do_read();
        }
    });
}

void tcp_session::do_write(const std::string& message)
{
    auto self(shared_from_this());
    std::cout << "Sent: " << message << "\n" << std::flush;

    asio::async_write(m_socket, asio::buffer(message), [this, self, message](asio::error_code ec, size_t) {
        if (ec)
        {
            std::cerr << "Failed to send the acknowledgment: " << ec.message() << "\n";
        }
        else
        {
            m_last_ongoing_message.data = message;
            m_last_ongoing_message.waiting_ack = true;
            m_last_ongoing_message.timestamp = std::chrono::steady_clock::now();
            start_ack_timer();
        }
    });
}

void tcp_session::close_connection(const std::string& reason)
{
    std::cerr << "Connection closed: " << reason << "\n";
    m_socket.close();
}

// Implementación de udp_server
udp_server::udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint,
                       database_manager& db_manager, config& config_params)
    : m_socket(io_context), m_config_params(config_params), m_database_manager(db_manager)
{
    m_socket.open(endpoint.protocol());
    m_socket.set_option(asio::socket_base::reuse_address(true));
    m_socket.bind(endpoint);

    do_receive();
}

bool udp_server::handle_udp_message(const std::string& message, const asio::ip::udp::endpoint& sender_endpoint)
{
    char* type = get_type(message.c_str());
    if (!type)
    {
        free(type);
        return 1;
    }
    if (strcmp(type, "client_auth_request") == 0)
    {
        do_auth_udp(message, m_sender_endpoint);
        free(type);
        return 0;
    }

    int type_code = get_message_code(type);
    free(type);
    switch (type_code)
    {
    case CLIENT_KEEPALIVE: {
        client_keepalive keepalive = deserialize_client_keepalive(message.c_str());

        if (!check_auth(keepalive.payload.username))
        {
            return 1;
        }
        break;
        //
    }
    case CLIENT_INVENTORY_UPDATE: {
        client_inventory_update inventory = deserialize_client_inventory_update(message.c_str());
        // Procesar update
        break;
    }
    case CLIENT_ACKNOWLEDGMENT: {
        client_acknowledgment ack = deserialize_client_acknowledgment(message.c_str());
        auto iterator = m_client_map.find(ack.payload.username);
        if (iterator == m_client_map.end())
        {
            return 1;
        }
        udp_client& client = *iterator->second;
        client.udp_last_ongoing_message->waiting_ack = false;
        client.udp_last_ongoing_message->data.clear();
        client.udp_last_ongoing_message->ack_timer.cancel();
        break;
    }
    case CLIENT_INFECTION_ALERT: {
        client_infection_alert alert = deserialize_client_infection_alert(message.c_str());
        // Procesar alerta
        break;
    }
    case WAREHOUSE_SEND_STOCK_TO_HUB: {
        warehouse_send_stock_to_hub msg = deserialize_warehouse_send_stock_to_hub(message.c_str());
        // Procesar envío
        break;
    }
    case WAREHOUSE_REQUEST_STOCK: {
        warehouse_request_stock req = deserialize_warehouse_request_stock(message.c_str());
        // Procesar request
        break;
    }
    case HUB_REQUEST_STOCK: {
        hub_request_stock hub_req = deserialize_hub_request_stock(message.c_str());
        // Procesar hub request
        break;
    }
    default:
        std::cerr << "Mensaje desconocido recibido por UDP\n";
        break;
    }
    return 0;
}

void udp_server::do_receive()
{
    m_socket.async_receive_from(asio::buffer(m_data), m_sender_endpoint, [this](asio::error_code ec, size_t length) {
        if (!ec)
        {
            std::string received_data_str(m_data.data(), length);

            handle_udp_message(received_data_str, m_sender_endpoint);
        }

        do_receive();
    });
}

void udp_server::do_send(const std::string& message, const asio::ip::udp::endpoint& endpoint,
                         const std::string& username)
{
    auto send_buffer = std::make_shared<std::array<char, BUFFER_SIZE>>();
    std::copy(message.begin(), message.end(), send_buffer->begin());

    m_socket.async_send_to(asio::buffer(*send_buffer, message.size()), endpoint,
                           [this, username, message](asio::error_code ec, std::size_t) {
                               if (ec)
                               {
                                   std::cerr << "UDP send failed to " << username << ": " << ec.message() << "\n";
                                   return;
                               }

                               auto iterator = m_client_map.find(username);
                               if (iterator != m_client_map.end())
                               {
                                   udp_client& client = *iterator->second;
                                   client.udp_last_ongoing_message->data = message;
                                   client.udp_last_ongoing_message->timestamp = std::chrono::steady_clock::now();
                                   client.udp_last_ongoing_message->waiting_ack = true;

                                   start_udp_ack_timer(username);
                               }
                           });
}

void udp_server::do_auth_udp(const std::string& msg, asio::ip::udp::endpoint sender_endpoint)
{
    client_auth_request req = deserialize_client_auth_request(msg.c_str());

    if (register_auth_attempt(req.payload.username))
    {
        return;
    }

    bool valid_credentials = m_database_manager.authenticate_client(req.payload.username, req.payload.password);
    std::string json = build_auth_response_json(valid_credentials,
                                                valid_credentials ? "Logged in successfully" : "Invalid credentials");

    auto send_buffer = std::make_shared<std::array<char, DATA_BUFFER_SIZE>>();
    copy_response_to_buffer(json, *send_buffer);

    m_socket.async_send_to(asio::buffer(*send_buffer, json.size()), sender_endpoint,
                           [this](asio::error_code ec, size_t) {
                               if (ec)
                               {
                                   std::cerr << "Failed to send the data: " << ec.message() << "\n";
                               }
                           });

    if (valid_credentials)
    {

        m_auth_attempts_map.erase(req.payload.username);

        auto client = std::make_unique<udp_client>(static_cast<asio::io_context&>(m_socket.get_executor().context()));
        client->username = req.payload.username;
        client->client_type = req.payload.type;
        client->endpoint = sender_endpoint;
        client->authenticated = true;

        m_client_map[req.payload.username] = std::move(client);
    }

    return;
}

bool udp_server::check_auth(const std::string& username)
{
    auto iterator = m_client_map.find(username);
    if (iterator == m_client_map.end())
    {
        return false;
    }
    udp_client& client = *iterator->second;
    if (!client.authenticated)
    {
        return false;
    }
    return true;
}

bool udp_server::register_auth_attempt(const std::string& username)
{
    if (m_auth_attempts_map.find(username) == m_auth_attempts_map.end())
    {
        if (m_auth_attempts_map.size() >= m_config_params.max_auth_attempts_map_size)
        {
            std::cout << "Max auth attempts map size reached, removing oldest entry\n" << std::flush;
            const std::string& oldest = m_auth_attempts_fifo.front();
            m_auth_attempts_map.erase(oldest);
            m_auth_attempts_fifo.pop();
        }
        m_auth_attempts_fifo.push(username);
        m_auth_attempts_map[username] = 1;
    }
    else
    {
        if (m_auth_attempts_map[username] <= m_config_params.max_auth_attempts)
        {
            m_auth_attempts_map[username]++;
        }
    }
    return m_auth_attempts_map[username] > m_config_params.max_auth_attempts;
}

void udp_server::start_udp_ack_timer(const std::string& username)
{
    auto iterator = m_client_map.find(username);
    if (iterator == m_client_map.end())
        return;

    udp_client& client = *iterator->second;

    client.udp_last_ongoing_message->ack_timer.expires_after(std::chrono::seconds(m_config_params.ack_timeout));
    client.udp_last_ongoing_message->ack_timer.async_wait([this, username](const asio::error_code& ec) {
        auto iterator = m_client_map.find(username);
        if (iterator == m_client_map.end())
            return;

        udp_client& client = *iterator->second;

        if (!ec && client.udp_last_ongoing_message->waiting_ack)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - client.udp_last_ongoing_message->timestamp);

            if (elapsed.count() >= m_config_params.ack_timeout)
            {
                do_send(client.udp_last_ongoing_message->data, client.endpoint, username);
            }
        }
        else if (ec != asio::error::operation_aborted)
        {
            std::cerr << "Error en ack timer UDP: " << ec.message() << "\n";
        }
    });
}

// Implementación de server
server::server(asio::io_context& io_context, config& config_params)
    : m_io_context(io_context), m_config_params(config_params),
      // TCP IPv4
      m_tcp4_acceptor(io_context,
                      asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port_tcp_v4)),
      // TCP IPv6
      m_tcp6_acceptor(io_context,
                      asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port_tcp_v6)),
      // UDP IPv4
      m_udp4_server(io_context,
                    asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port_udp_v4),
                    m_database_manager, config_params),
      // UDP IPv6
      m_udp6_server(io_context,
                    asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port_udp_v6),
                    m_database_manager, config_params)

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
            std::cout << "Nueva conexión desde: " << socket.remote_endpoint() << "\n" << std::flush;
            std::make_shared<tcp_session>(std::move(socket), m_database_manager, m_config_params)->start();
        }
        else
        {
            // log_error("Error en accept: %s", ec.message().c_str());

            std::cerr << "Error en accept: " << ec.message() << "\n";
        }
        do_accept();
    });

    m_tcp6_acceptor.async_accept([this](asio::error_code ec, asio::ip::tcp::socket socket) {
        if (!ec)
        {
            std::cout << "Nueva conexión desde: " << socket.remote_endpoint() << "\n" << std::flush;
            std::make_shared<tcp_session>(std::move(socket), m_database_manager, m_config_params)->start();
        }
        else
        {
            std::cerr << "Error en accept: " << ec.message() << "\n";
        }
        do_accept();
    });
}

// refactorizar esto para evitar duplicación con el ack timer de udp
void tcp_session::start_ack_timer()
{
    m_last_ongoing_message.ack_timer.expires_after(std::chrono::seconds(m_config_params.ack_timeout));
    m_last_ongoing_message.ack_timer.async_wait([this, self = shared_from_this()](const asio::error_code& ec) {
        if (!ec && m_last_ongoing_message.waiting_ack)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_ongoing_message.timestamp);

            if (elapsed.count() >= m_config_params.ack_timeout)
            {
                do_write(m_last_ongoing_message.data);
            }
        }

        else if (ec != asio::error::operation_aborted)
        {
            std::cerr << "Error in ack timer: " << ec.message() << "\n";
        }
    });
}
