#include "dhl_server.hpp"
#include "database.hpp"
#include "json_manager.h"
#include "logger.h"
#include "utilities.hpp"
#include <client_sender.hpp>
#include <iostream>
#include <memory>
#include <pqxx/pqxx>

// Implementacion de tcp_session
tcp_session::tcp_session(asio::ip::tcp::socket socket, database_manager& db, server& main_server, config& config_params)
    : m_socket(std::move(socket)), m_config_params(config_params), m_database_manager(db), m_server(main_server),
      m_last_ongoing_message(static_cast<asio::io_context&>(m_socket.get_executor().context())),
      m_keepalive_timer(static_cast<asio::io_context&>(m_socket.get_executor().context()))
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

        if (m_server.is_client_active_tcp(req.payload.username))
        {
            close_connection("Client already logged in.");
            return;
        }

        bool valid_credentials = m_database_manager.authenticate_client(req.payload.username, req.payload.password);
        std::string json = build_auth_response_json(valid_credentials, valid_credentials ? "Logged in successfully"
                                                                                         : "Invalid credentials");
        // copy_response_to_buffer(json, m_data);

        if (m_auth_attempts > m_config_params.max_auth_attempts)
        {
            close_connection("Max authentication attempts reached.");
            return;
        }

        asio::async_write(m_socket, asio::buffer(json, json.size()), [this, self, length](asio::error_code ec, size_t) {
            if (ec)
            {
                std::cerr << "Failed to send the data: " << ec.message() << "\n";
            }
        });

        m_auth_attempts++;

        if (valid_credentials)
        {
            m_username = req.payload.username;
            m_server.register_tcp_client(m_username, shared_from_this());
            start_keepalive_timer();

            m_database_manager.set_client_active(req.payload.username);

            // Send inventory update to client
            static inventory_item items[ITEM_TYPE] = {{"water", 0}, {"food", 0}, {"medicine", 0},
                                                      {"guns", 0},  {"ammo", 0}, {"tools", 0}};
            m_database_manager.retrieve_client_inventory(req.payload.username, items);
            client_inventory_update inventory_update =
                create_client_inventory_update(req.payload.username, "token", items, ITEM_TYPE);
            std::string inventory_json = serialize_client_inventory_update(&inventory_update, ITEM_TYPE);
            do_write(inventory_json);

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
        // Handle keepalive message, if not received, unregister client and close connection
        std::cout << "Received keepalive tcp message\n" << std::flush;
        // Reset the keepalive timer
        reset_keepalive_timer();

        break;
    }
    case CLIENT_INVENTORY_UPDATE: {
        // Handle inventory update message
        std::cout << "Received inventory update message\n" << std::flush;
        client_inventory_update inventory_update = deserialize_client_inventory_update(message.c_str());
        m_database_manager.update_client_inventory(
            inventory_update.payload.username, inventory_update.payload.items[0].quantity,
            inventory_update.payload.items[1].quantity, inventory_update.payload.items[2].quantity,
            inventory_update.payload.items[3].quantity, inventory_update.payload.items[4].quantity,
            inventory_update.payload.items[5].quantity);

        break;
    }
    case CLIENT_ACKNOWLEDGEMENT: {
        // Process acknowledgment message
        client_acknowledgment ack = deserialize_client_acknowledgment(message.c_str());
        m_last_ongoing_message.waiting_ack = false;
        m_last_ongoing_message.data.clear();
        m_last_ongoing_message.ack_timer.cancel();
        break;
    }
    case CLIENT_INFECTION_ALERT:
        // Handle infection alert message
        // depending on the type of alert, send supplies to the client to overcome the emergency

        break;
    case WAREHOUSE_SEND_STOCK_TO_HUB: {
        // Handle send stock to hub message
        // a warehouse notifies the server of a dispatch to a hub
        // server should notify the hub and register the transaction in the database

        warehouse_send_stock_to_hub stock_shipment = deserialize_warehouse_send_stock_to_hub(message.c_str());
        std::cout << "Received stock shipment from warehouse to hub\n" << std::flush;

        // Notify the hub
        std::shared_ptr<client_sender> hub_sender = get_client_sender(stock_shipment.payload.hub_username, m_server);
        if (hub_sender)
        {
            std::string stock_shipment_notification = build_stock_shipment_notify_json(stock_shipment);
            hub_sender->send(stock_shipment_notification);
        }
        else
        {
            std::cerr << "Hub not found: " << stock_shipment.payload.hub_username << "\n";
        }

        // register the transaction in the database
        m_database_manager.register_warehouse_shipment(
            stock_shipment.payload.username, stock_shipment.payload.hub_username,
            stock_shipment.payload.items[0].quantity, stock_shipment.payload.items[1].quantity,
            stock_shipment.payload.items[2].quantity, stock_shipment.payload.items[3].quantity,
            stock_shipment.payload.items[4].quantity, stock_shipment.payload.items[5].quantity,
            stock_shipment.payload.timestamp);
        break;
    }
    case WAREHOUSE_REQUEST_STOCK: {
        // Handle request stock message
        // a warehouse requests stock from the server
        // server should authorize the stock request and notify the warehouse
        warehouse_request_stock warehouse_stock_request = deserialize_warehouse_request_stock(message.c_str());
        std::cout << "Received stock request from warehouse\n" << std::flush;
        std::shared_ptr<client_sender> sender = get_client_sender(warehouse_stock_request.payload.username, m_server);
        if (sender)
        {
            std::string stock_warehouse = build_stock_warehouse_json(warehouse_stock_request);
            sender->send(stock_warehouse);
        }
        // register the transaction in the database
        m_database_manager.register_warehouse_stock_request(
            warehouse_stock_request.payload.username, warehouse_stock_request.payload.items[0].quantity,
            warehouse_stock_request.payload.items[1].quantity, warehouse_stock_request.payload.items[2].quantity,
            warehouse_stock_request.payload.items[3].quantity, warehouse_stock_request.payload.items[4].quantity,
            warehouse_stock_request.payload.items[5].quantity, warehouse_stock_request.payload.timestamp);
        break;
    }
    case HUB_REQUEST_STOCK: {
        // Handle hub request stock message
        // a hub requests stock to the server
        // server should authorize the stock request and notify a hub, registering the transaction in the database
        hub_request_stock hub_stock_request = deserialize_hub_request_stock(message.c_str());
        std::cout << "Received stock request from hub\n" << std::flush;
        // Notify the warehouse
        std::string warehouse_username = m_database_manager.get_designated_warehouse(
            hub_stock_request.payload.items[0].quantity, hub_stock_request.payload.items[1].quantity,
            hub_stock_request.payload.items[2].quantity, hub_stock_request.payload.items[3].quantity,
            hub_stock_request.payload.items[4].quantity, hub_stock_request.payload.items[5].quantity);
        if (warehouse_username.empty())
        {
            // No warehouse found for the requested stock
            // maybe set a timer for the server to retry finding a warehouse

            std::cerr << "No warehouse found for the requested stock\n";
            return;
        }

        std::shared_ptr<client_sender> warehouse_sender = get_client_sender(warehouse_username, m_server);
        if (warehouse_sender)
        {
            std::string new_order_notification = build_placed_order_json(hub_stock_request);
            warehouse_sender->send(new_order_notification);
        }
        else
        {
            std::cerr << "Warehouse not found: " << warehouse_username << "\n";
        }

        // register the transaction in the database
        m_database_manager.register_hub_order(
            hub_stock_request.payload.username, warehouse_username, hub_stock_request.payload.items[0].quantity,
            hub_stock_request.payload.items[1].quantity, hub_stock_request.payload.items[2].quantity,
            hub_stock_request.payload.items[3].quantity, hub_stock_request.payload.items[4].quantity,
            hub_stock_request.payload.items[5].quantity, hub_stock_request.payload.timestamp);
        break;
    }
    case HUB_CONFIRM_STOCK: {
        // Handle hub confirm stock message
        // a hub confirms the stock received from a warehouse
        // server should register the transaction in the database and update hub inventory
        hub_confirm_stock hub_stock_confirm = deserialize_hub_confirm_stock(message.c_str());
        std::cout << "Received stock confirmation from hub\n" << std::flush;
        // register the transaction in the database
        m_database_manager.register_hub_stock_confirm(
            hub_stock_confirm.payload.warehouse_username, hub_stock_confirm.payload.username,
            hub_stock_confirm.payload.items[0].quantity, hub_stock_confirm.payload.items[1].quantity,
            hub_stock_confirm.payload.items[2].quantity, hub_stock_confirm.payload.items[3].quantity,
            hub_stock_confirm.payload.items[4].quantity, hub_stock_confirm.payload.items[5].quantity,
            hub_stock_confirm.payload.timestamp);
        break;
    }
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
            // Acumula los datos recibidos en el buffer
            m_tcp_buffer.append(m_data.data(), length);

            size_t pos;
            // Procesa todos los mensajes completos (delimitados por '\n')
            while ((pos = m_tcp_buffer.find('\n')) != std::string::npos)
            {
                std::string message = m_tcp_buffer.substr(0, pos);
                m_tcp_buffer.erase(0, pos + 1); // Elimina el mensaje procesado y el delimitador

                std::cout << "Raw received TCP message: " << message << "\n" << std::flush;
                handle_tcp_message(message);
            }

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
                       database_manager& db_manager, server& main_server, config& config_params)
    : m_socket(io_context), m_config_params(config_params), m_database_manager(db_manager), m_server(main_server)
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

        if (!m_server.is_client_active_udp(keepalive.payload.username))
        {
            // no se tienen en cuenta los siguientes keepalive ni los otros mensajes
            return 1;
        }
        std::cout << "Received keepalive udp message\n" << std::flush;
        // si recibo un keepalive, reseteo el timer de 65 segundos
        reset_keepalive_timer(keepalive.payload.username);

        break;
        //
    }
    case CLIENT_INVENTORY_UPDATE: {
        client_inventory_update inventory_update = deserialize_client_inventory_update(message.c_str());

        if (!m_server.is_client_active_udp(inventory_update.payload.username))
        {
            // no se tiene en cuenta porque no registro su keepalive a tiempo
            return 1;
        }
        std::cout << "Received inventory update message\n" << std::flush;
        // actualizo el inventario del cliente
        m_database_manager.update_client_inventory(
            inventory_update.payload.username, inventory_update.payload.items[0].quantity,
            inventory_update.payload.items[1].quantity, inventory_update.payload.items[2].quantity,
            inventory_update.payload.items[3].quantity, inventory_update.payload.items[4].quantity,
            inventory_update.payload.items[5].quantity);

        break;
    }
    case CLIENT_ACKNOWLEDGEMENT: {
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
        // client_emergency_alert alert = deserialize_server_emergency_alert(message.c_str());
        // Procesar alerta

        break;
    }
    case WAREHOUSE_SEND_STOCK_TO_HUB: {
        warehouse_send_stock_to_hub stock_shipment = deserialize_warehouse_send_stock_to_hub(message.c_str());
        // Procesar envío
        if (!m_server.is_client_active_udp(stock_shipment.payload.username))
        {
            // no se tiene en cuenta porque no registro su keepalive a tiempo
            return 1;
        }
        std::cout << "Received stock shipment from warehouse to hub\n" << std::flush;

        // Notify the hub
        std::shared_ptr<client_sender> hub_sender = get_client_sender(stock_shipment.payload.hub_username, m_server);
        if (hub_sender)
        {
            std::string stock_shipment_notification = build_stock_shipment_notify_json(stock_shipment);
            hub_sender->send(stock_shipment_notification);
        }
        else
        {
            std::cerr << "Hub not found: " << stock_shipment.payload.hub_username << "\n";
        }
        // Register the transaction in the database
        m_database_manager.register_warehouse_shipment(
            stock_shipment.payload.username, stock_shipment.payload.hub_username,
            stock_shipment.payload.items[0].quantity, stock_shipment.payload.items[1].quantity,
            stock_shipment.payload.items[2].quantity, stock_shipment.payload.items[3].quantity,
            stock_shipment.payload.items[4].quantity, stock_shipment.payload.items[5].quantity,
            stock_shipment.payload.timestamp);

        break;
    }
    case WAREHOUSE_REQUEST_STOCK: {
        // Handle request stock message
        // a warehouse requests stock from the server
        // server should authorize the stock request and notify the warehouse
        warehouse_request_stock warehouse_stock_request = deserialize_warehouse_request_stock(message.c_str());
        std::cout << "Received stock request from warehouse\n" << std::flush;
        std::shared_ptr<client_sender> sender = get_client_sender(warehouse_stock_request.payload.username, m_server);
        if (sender)
        {
            std::string stock_warehouse = build_stock_warehouse_json(warehouse_stock_request);
            std::cout << "Sending stock warehouse json to: " << warehouse_stock_request.payload.username << std::endl;
            sender->send(stock_warehouse);
        }

        // register the transaction in the database
        m_database_manager.register_warehouse_stock_request(
            warehouse_stock_request.payload.username, warehouse_stock_request.payload.items[0].quantity,
            warehouse_stock_request.payload.items[1].quantity, warehouse_stock_request.payload.items[2].quantity,
            warehouse_stock_request.payload.items[3].quantity, warehouse_stock_request.payload.items[4].quantity,
            warehouse_stock_request.payload.items[5].quantity, warehouse_stock_request.payload.timestamp);

        break;
    }

    case HUB_REQUEST_STOCK: {
        // Handle hub request stock message
        // a hub requests stock to the server
        // server should authorize the stock request and notify a hub, registering the transaction in the database
        hub_request_stock hub_stock_request = deserialize_hub_request_stock(message.c_str());
        if (!m_server.is_client_active_udp(hub_stock_request.payload.username))
        {
            // no se tiene en cuenta porque no registro su keepalive a tiempo
            return 1;
        }
        std::cout << "Received stock request from hub\n" << std::flush;
        // Notify the warehouse
        std::string warehouse_username = m_database_manager.get_designated_warehouse(
            hub_stock_request.payload.items[0].quantity, hub_stock_request.payload.items[1].quantity,
            hub_stock_request.payload.items[2].quantity, hub_stock_request.payload.items[3].quantity,
            hub_stock_request.payload.items[4].quantity, hub_stock_request.payload.items[5].quantity);
        if (warehouse_username.empty())
        {
            // No warehouse found for the requested stock
            // maybe set a timer for the server to retry finding a warehouse

            std::cerr << "No warehouse found for the requested stock\n";
            return 1;
        }

        std::shared_ptr<client_sender> warehouse_sender = get_client_sender(warehouse_username, m_server);
        if (warehouse_sender)
        {
            std::string new_order_notification = build_placed_order_json(hub_stock_request);
            warehouse_sender->send(new_order_notification);
        }
        else
        {
            std::cerr << "Warehouse not found: " << warehouse_username << "\n";
        }

        // register the transaction in the database
        m_database_manager.register_hub_order(
            hub_stock_request.payload.username, warehouse_username, hub_stock_request.payload.items[0].quantity,
            hub_stock_request.payload.items[1].quantity, hub_stock_request.payload.items[2].quantity,
            hub_stock_request.payload.items[3].quantity, hub_stock_request.payload.items[4].quantity,
            hub_stock_request.payload.items[5].quantity, hub_stock_request.payload.timestamp);
        break;
    }
    case HUB_CONFIRM_STOCK: {
        // Handle hub confirm stock message
        // a hub confirms the stock received from a warehouse
        // server should register the transaction in the database and update hub inventory
        hub_confirm_stock hub_stock_confirm = deserialize_hub_confirm_stock(message.c_str());
        std::cout << "Received stock confirmation from hub\n" << std::flush;
        // register the transaction in the database
        m_database_manager.register_hub_stock_confirm(
            hub_stock_confirm.payload.warehouse_username, hub_stock_confirm.payload.username,
            hub_stock_confirm.payload.items[0].quantity, hub_stock_confirm.payload.items[1].quantity,
            hub_stock_confirm.payload.items[2].quantity, hub_stock_confirm.payload.items[3].quantity,
            hub_stock_confirm.payload.items[4].quantity, hub_stock_confirm.payload.items[5].quantity,
            hub_stock_confirm.payload.timestamp);
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
            std::cout << "Raw received UDP message: " << received_data_str << "\n" << std::flush;

            handle_udp_message(received_data_str, m_sender_endpoint);
        }

        do_receive();
    });
}

void udp_server::do_send(const std::string& message, const asio::ip::udp::endpoint& endpoint,
                         const std::string& username)
{
    auto send_buffer = std::make_shared<std::array<char, SERVER_BUFFER_SIZE>>();
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
    std::cout << "Received auth request from: " << req.payload.username << std::endl;
    if (m_server.is_client_active_udp(req.payload.username)) // si ya esta activo, no lo vuelvo a registrar
    {
        std::cerr << "Client already logged in.\n";
        return;
    }
    if (register_auth_attempt(req.payload.username))
    {
        return;
    }

    std::cout << "Authenticating client: " << req.payload.username << std::endl;
    bool valid_credentials = m_database_manager.authenticate_client(req.payload.username, req.payload.password);
    std::cout << "Authentication result: " << valid_credentials << std::endl;
    std::string json = build_auth_response_json(valid_credentials,
                                                valid_credentials ? "Logged in successfully" : "Invalid credentials");

    std::cout << "Sending auth response to: " << req.payload.username << std::endl;

    auto send_buffer = std::make_shared<std::array<char, DATA_BUFFER_SIZE>>();
    copy_response_to_buffer(json, *send_buffer);

    m_socket.async_send_to(asio::buffer(*send_buffer, json.size()), sender_endpoint,
                           [this](asio::error_code ec, size_t) {
                               if (ec)
                               {
                                   std::cerr << "Failed to send the data: " << ec.message() << "\n";
                               }
                           });

    std::cout << "Valid credentials: " << valid_credentials << std::endl;
    if (valid_credentials)
    {
        std::cout << "Erasing auth attempts for: " << req.payload.username << std::endl;
        m_auth_attempts_map.erase(req.payload.username);

        auto client = std::make_unique<udp_client>(static_cast<asio::io_context&>(m_socket.get_executor().context()));
        client->username = req.payload.username;
        client->client_type = req.payload.type;
        client->endpoint = sender_endpoint;
        client->authenticated = true;

        std::cout << "Registering UDP client: " << req.payload.username << std::endl;

        m_client_map[req.payload.username] = std::move(client);
        m_server.register_udp_client(req.payload.username, sender_endpoint);

        start_keepalive_timer(req.payload.username);

        m_database_manager.set_client_active(req.payload.username);

        // Send inventory update to client
        static inventory_item items[ITEM_TYPE] = {{"water", 0}, {"food", 0}, {"medicine", 0},
                                                      {"guns", 0},  {"ammo", 0}, {"tools", 0}};
        m_database_manager.retrieve_client_inventory(req.payload.username, items);
        client_inventory_update inventory_update =
            create_client_inventory_update(req.payload.username, "token", items, ITEM_TYPE);
        std::string inventory_json = serialize_client_inventory_update(&inventory_update, ITEM_TYPE);
        do_send(inventory_json, sender_endpoint, req.payload.username);
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
      m_tcp4_acceptor(io_context,
                      asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port_tcp_v4)),
      m_tcp6_acceptor(io_context,
                      asio::ip::tcp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port_tcp_v6)),
      m_database_manager(),
      m_udp4_server(std::make_unique<udp_server>(
          io_context, asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v4), config_params.port_udp_v4),
          m_database_manager, *this, config_params)),
      m_udp6_server(std::make_unique<udp_server>(
          io_context, asio::ip::udp::endpoint(asio::ip::make_address(config_params.ip_v6), config_params.port_udp_v6),
          m_database_manager, *this, config_params))
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
            std::make_shared<tcp_session>(std::move(socket), m_database_manager, *this, m_config_params)->start();
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
            std::make_shared<tcp_session>(std::move(socket), m_database_manager, *this, m_config_params)->start();
        }
        else
        {
            std::cerr << "Error en accept: " << ec.message() << "\n";
        }
        do_accept();
    });
}

void server::register_tcp_client(const std::string& username, std::shared_ptr<tcp_session> client)
{
    m_active_tcp_clients[username] = client;
    m_database_manager.set_client_active(username);
}

void server::unregister_tcp_client(const std::string& username)
{
    m_active_tcp_clients.erase(username);
    m_database_manager.set_client_inactive(username);
}
void server::register_udp_client(const std::string& username, const asio::ip::udp::endpoint& endpoint)
{
    m_active_udp_clients[username] = endpoint;
    m_database_manager.set_client_active(username);
}

void server::unregister_udp_client(const std::string& username)
{
    m_active_udp_clients.erase(username);
    m_database_manager.set_client_inactive(username);
}

bool server::is_client_active_udp(const std::string& username)
{
    auto it = m_active_udp_clients.find(username);
    if (it != m_active_udp_clients.end())
    {
        return true;
    }
    return false;
}

bool server::is_client_active_tcp(const std::string& username)
{
    return m_active_tcp_clients.find(username) != m_active_tcp_clients.end();
}

std::string tcp_session::get_username()
{
    return m_username;
}

std::string tcp_session::get_client_type()
{
    return m_client_type;
}

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

void tcp_session::start_keepalive_timer()
{
    auto self = shared_from_this();
    m_keepalive_timer.expires_after(m_keepalive_timeout);
    m_keepalive_timer.async_wait([self](const asio::error_code& ec) {
        if (!ec)
        {
            std::cerr << "Keepalive timeout for client. \n";
            self->m_server.unregister_tcp_client(self->get_username());
            self->close_connection("Keepalive timeout");
        }
    });
}

void tcp_session::reset_keepalive_timer()
{
    m_keepalive_timer.cancel();
    start_keepalive_timer();
}

void udp_server::start_keepalive_timer(const std::string& username)
{
    auto iterator = m_client_map.find(username);
    if (iterator == m_client_map.end())
        return;
    udp_client& client = *iterator->second;

    client.keepalive_timer.expires_after(client.keepalive_timeout);
    client.keepalive_timer.async_wait([this, username](const asio::error_code& ec) {
        if (!ec)
        {
            std::cerr << "Keepalive timeout for client. \n";
            // Unregister client and close connection
            m_server.unregister_udp_client(username);
        }
    });
}

void udp_server::reset_keepalive_timer(const std::string& username)
{
    auto iterator = m_client_map.find(username);
    if (iterator == m_client_map.end())
        return;
    udp_client& client = *iterator->second;

    client.keepalive_timer.cancel();
    start_keepalive_timer(username);
}

std::shared_ptr<tcp_session> server::get_tcp_session(const std::string& username)
{
    auto it = m_active_tcp_clients.find(username);
    if (it != m_active_tcp_clients.end())
    {
        return it->second;
    }
    return nullptr;
}

asio::ip::udp::endpoint server::get_udp_endpoint(const std::string& username)
{
    auto it = m_active_udp_clients.find(username);
    if (it != m_active_udp_clients.end())
    {
        return it->second;
    }
    return asio::ip::udp::endpoint();
}

udp_server* server::get_udp_server(asio::ip::udp::endpoint endpoint)
{
    // if endpoint is v4
    if (endpoint.address().is_v4())
        return m_udp4_server.get();
    if (endpoint.address().is_v6())
        return m_udp6_server.get();
    return nullptr;
}

void udp_server::send_msg(const std::string& message, const asio::ip::udp::endpoint& endpoint,
                          const std::string& username)
{
    do_send(message, endpoint, username);
}

void tcp_session::send_msg(const std::string& message)
{
    do_write(message);
}

void server::shutdown()
{
    // Cerrar todas las conexiones TCP activas
    for (auto& [username, session] : m_active_tcp_clients)
    {
        if (session)
        {
            session->close_connection("Server shutting down");
            session.reset(); // Liberar la sesión
        }
    }
    m_active_tcp_clients.clear();

    // Cerrar los aceptadores TCP
    m_tcp4_acceptor.close();
    m_tcp6_acceptor.close();

    // Cerrar los servidores UDP
    if (m_udp4_server)
    {
        m_udp4_server->m_socket.close();
        m_udp4_server.reset(); // Liberar el servidor UDP
    }
    if (m_udp6_server)
    {
        m_udp6_server->m_socket.close();
        m_udp6_server.reset(); // Liberar el servidor UDP
    }
    m_active_udp_clients.clear();
}
