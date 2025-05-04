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

        std::cout << "Received auth request: " << std::string(m_data.data(), length) << "\n" << std::flush;

        std::cout << "Username: " << req.payload.username << "\n" << std::flush;
        std::cout << "Password: " << req.payload.password << "\n" << std::flush;
        bool valid_credentials = m_database_manager.authenticate_client(req.payload.username, req.payload.password);

        if (!valid_credentials)
        {
            m_auth_attempts++;
            if (m_auth_attempts > m_config_params.max_auth_attempts)
            {
                close_connection("Max authentication attempts reached.");
                return;
            }
            else
            {
                server_auth_response resp = create_server_auth_response("failure", // status
                                                                        "",        // session_token vacío
                                                                        "Invalid credentials");
                char* serialized = serialize_server_auth_response(&resp);
                std::string json_resp(serialized);
                free(serialized);

                std::copy(json_resp.begin(), json_resp.end(), m_data.begin());

                std::cout << "Sent: " << json_resp << "\n" << std::flush;

                asio::async_write(m_socket, asio::buffer(m_data, json_resp.size()),
                                  [this, self, length](asio::error_code ec, size_t) {
                                      if (ec)
                                      {
                                          std::cerr << "Failed to send the data: " << ec.message() << "\n";
                                      }
                                  });

                // Volver a esperar otro intento
                do_auth();
                return;
            }
        }

        server_auth_response resp = create_server_auth_response("success", "token", "Logged in successfully");
        char* serialized = serialize_server_auth_response(&resp);
        std::string json_resp(serialized);
        free(serialized);

        std::copy(json_resp.begin(), json_resp.end(), m_data.begin());

        std::cout << "Sent: " << json_resp << "\n" << std::flush;

        asio::async_write(m_socket, asio::buffer(m_data, json_resp.size()),
                          [this, self, length](asio::error_code ec, size_t) {
                              if (ec)
                              {
                                  std::cerr << "Failed to send the data: " << ec.message() << "\n";
                              }
                          });

        do_read();
    });
}

void tcp_session::do_read()
{
    auto self(shared_from_this());
    m_socket.async_read_some(asio::buffer(m_data), [this, self](asio::error_code ec, size_t length) {
        if (!ec)
        {
            std::cout << "Received: " << std::string(m_data.data(), length) << "\n" << std::flush;

            char* type = get_type(m_data.data());
            int type_code = get_message_code(type);

            switch (type_code)
            {
            case CLIENT_KEEPALIVE: {
                client_keepalive keepalive = deserialize_client_keepalive(m_data.data());
                // Process keepalive message
                server_emergency_alert alert = create_server_emergency_alert("infection_detected");
                char* serialized_alert = serialize_server_emergency_alert(&alert);
                std::string json_alert(serialized_alert);
                free(serialized_alert);

                do_write_ack(json_alert);

                break;
            }
            case CLIENT_INVENTORY_UPDATE: {
                client_inventory_update inventory_update = deserialize_client_inventory_update(m_data.data());
                // Process inventory update message
                break;
            }
            case CLIENT_ACKNOWLEDGMENT: {
                client_acknowledgment acknowledgment = deserialize_client_acknowledgment(m_data.data());
                // Process acknowledgment message
                client_acknowledgment ack = deserialize_client_acknowledgment(m_data.data());
                m_last_ongoing_message.waiting_ack = false;
                m_last_ongoing_message.data.clear();
                m_last_ongoing_message.ack_timer.cancel();
                break;
            }
            case CLIENT_INFECTION_ALERT: {
                client_infection_alert infection_alert = deserialize_client_infection_alert(m_data.data());
                // Process infection alert message
                break;
            }
            case WAREHOUSE_SEND_STOCK_TO_HUB: {
                warehouse_send_stock_to_hub send_stock = deserialize_warehouse_send_stock_to_hub(m_data.data());
                // Process send stock to hub message
                break;
            }
            case WAREHOUSE_REQUEST_STOCK: {
                warehouse_request_stock request_stock = deserialize_warehouse_request_stock(m_data.data());
                // Process request stock message
                break;
            }
            case HUB_REQUEST_STOCK: {
                hub_request_stock hub_request = deserialize_hub_request_stock(m_data.data());
                // Process hub request stock message
                break;
            }
            default:
                break;
            }

            do_read();
        }
    });
}

// void tcp_session::do_write(size_t length)
// {
//     auto self(shared_from_this());
//     std::cout << "Sent: " << std::string(m_data.data(), length) << "\n" << std::flush;
//     asio::async_write(m_socket, asio::buffer(m_data, length), [this, self, length](asio::error_code ec, size_t) {
//         if (ec)
//         {
//             std::cerr << "Failed to send the data: " << ec.message() << "\n";
//         }
//         else
//         {
//             m_last_ongoing_message.data = std::string(m_data.data(), length);
//             m_last_ongoing_message.waiting_ack = true;
//             m_last_ongoing_message.timestamp = std::chrono::steady_clock::now();
//             start_ack_timer();
//         }
//     });
// }

void tcp_session::do_write_ack(const std::string& message)
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

void udp_server::do_receive()
{
    std::cout << "Waiting for UDP messages...\n" << std::flush;
    m_socket.async_receive_from(asio::buffer(m_data), m_sender_endpoint, [this](asio::error_code ec, size_t length) {
        if (!ec)
        {

            std::cout << "Received: " << std::string(m_data.data(), length) << "\n" << std::flush;

            std::string received_data_str(m_data.data(), length);
            char* type = get_type(received_data_str.c_str()); // <- ¡No te olvides de hacer free!

            // Si el mensaje es de autenticación, lo procesamos primero
            if (type == nullptr)
            {
                do_receive();
                return;
            }
            if (strcmp(type, "client_auth_request") == 0)
            {
                do_auth_udp(received_data_str, m_sender_endpoint);
                free(type);
                do_receive();
                return;
            }

            int type_code = get_message_code(type);

            switch (type_code)
            {
            case CLIENT_KEEPALIVE: {
                client_keepalive keepalive = deserialize_client_keepalive(received_data_str.c_str());
                // Procesar keepalive
                if (!check_auth(keepalive.payload.username))
                {
                    free(type);
                    do_receive();
                    return;
                }

                server_emergency_alert alert = create_server_emergency_alert("infection_detected");
                char* serialized_alert = serialize_server_emergency_alert(&alert);
                std::string json_alert(serialized_alert);
                free(serialized_alert);

                do_send(json_alert, m_sender_endpoint, keepalive.payload.username);

                break;
            }
            case CLIENT_INVENTORY_UPDATE: {
                client_inventory_update inventory = deserialize_client_inventory_update(received_data_str.c_str());
                // Procesar update
                break;
            }
            case CLIENT_ACKNOWLEDGMENT: {
                client_acknowledgment ack = deserialize_client_acknowledgment(received_data_str.c_str());
                auto iterator = m_client_map.find(ack.payload.username);
                if (iterator == m_client_map.end())
                {
                    return;
                }
                udp_client& client = *iterator->second;

                client.udp_last_ongoing_message->waiting_ack = false;
                client.udp_last_ongoing_message->data.clear();
                client.udp_last_ongoing_message->ack_timer.cancel();
                break;
            }
            case CLIENT_INFECTION_ALERT: {
                client_infection_alert alert = deserialize_client_infection_alert(received_data_str.c_str());
                // Procesar alerta
                break;
            }
            case WAREHOUSE_SEND_STOCK_TO_HUB: {
                warehouse_send_stock_to_hub msg = deserialize_warehouse_send_stock_to_hub(received_data_str.c_str());
                // Procesar envío
                break;
            }
            case WAREHOUSE_REQUEST_STOCK: {
                warehouse_request_stock req = deserialize_warehouse_request_stock(received_data_str.c_str());
                // Procesar request
                break;
            }
            case HUB_REQUEST_STOCK: {
                hub_request_stock hub_req = deserialize_hub_request_stock(received_data_str.c_str());
                // Procesar hub request
                break;
            }
            default:
                std::cerr << "Mensaje desconocido recibido por UDP\n";
                break;
            }

            free(type);
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

// refactorizar esto para que no se repita el código de tcp
void udp_server::do_auth_udp(const std::string& msg, asio::ip::udp::endpoint sender_endpoint)
{

    client_auth_request req = deserialize_client_auth_request(msg.c_str());

    bool too_many_attempts = register_auth_attempt(req.payload.username);
    if (too_many_attempts)
    {
        std::cerr << "Max auth attempts for: " << req.payload.username << "\n";
        return;
    }

    bool valid_auth = m_database_manager.authenticate_client(req.payload.username, req.payload.password);

    if (valid_auth)
    {

        m_auth_attempts_map.erase(req.payload.username);

        auto client = std::make_unique<udp_client>(static_cast<asio::io_context&>(m_socket.get_executor().context()));
        client->username = req.payload.username;
        client->client_type = req.payload.type;
        client->endpoint = sender_endpoint;
        // client.udp_last_ongoing_message = udp_last_ongoing_message(m_socket.get_executor());
        client->authenticated = true;

        m_client_map[req.payload.username] = std::move(client);

        server_auth_response response = create_server_auth_response("success", "token", "Logged in successfully");
        char* serialized = serialize_server_auth_response(&response);
        std::string json_response(serialized);
        free(serialized);

        auto send_buffer = std::make_shared<std::array<char, BUFFER_SIZE>>();
        std::copy(json_response.begin(), json_response.end(), send_buffer->begin());

        std::cout << "Sent: " << json_response << "\n" << std::flush;

        m_socket.async_send_to(asio::buffer(*send_buffer, json_response.size()), sender_endpoint,
                               [this](asio::error_code ec, size_t) {
                                   if (ec)
                                   {
                                       std::cerr << "Failed to send the data: " << ec.message() << "\n";
                                   }
                               });
        return;
    }
    else
    {
        server_auth_response response = create_server_auth_response("failure", "", "Invalid credentials");
        char* serialized = serialize_server_auth_response(&response);
        std::string json_response(serialized);
        free(serialized);

        auto send_buffer = std::make_shared<std::array<char, BUFFER_SIZE>>();
        std::copy(json_response.begin(), json_response.end(), send_buffer->begin());

        std::cout << "Sent: " << json_response << "\n" << std::flush;

        m_socket.async_send_to(asio::buffer(*send_buffer, json_response.size()), sender_endpoint,
                               [this](asio::error_code ec, size_t) {
                                   if (ec)
                                   {
                                       std::cerr << "Failed to send the data: " << ec.message() << "\n";
                                   }
                               });
        return;
    }
    return;
}

bool udp_server::check_auth(const std::string& username)
{
    auto iterator = m_client_map.find(username);
    if (iterator == m_client_map.end())
    {
        std::cerr << "Client not authenticated: " << username << "\n";
        return false;
    }
    udp_client& client = *iterator->second;
    if (!client.authenticated)
    {
        std::cerr << "Client not authenticated: " << username << "\n";
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
// server::server(asio::io_context& io_context, config& config_params)
//     : m_io_context(io_context),

//       m_config_params(config_params),

//       //   m_tcp4_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 9999)),
//       m_tcp4_acceptor(io_context,
//                       asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port)),

//       //  m_tcp6_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v6(), 9999)),
//       m_tcp6_acceptor(io_context,
//                       asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port)),

//       //   m_udp4_server(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 9999), m_database_manager),
//       m_udp4_server(io_context,
//                     asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port),
//                     m_database_manager, config_params),

//       //  m_udp6_server(io_context, asio::ip::udp::endpoint(asio::ip::udp::v6(), 9999), m_database_manager)
//       m_udp6_server(io_context,
//                     asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port),
//                     m_database_manager, config_params)

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
                do_write_ack(m_last_ongoing_message.data);
            }
        }

        else if (ec != asio::error::operation_aborted)
        {
            std::cerr << "Error in ack timer: " << ec.message() << "\n";
        }
    });
}
