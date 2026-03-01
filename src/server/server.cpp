#include "server.hpp"

#include <common/json_manager.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

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

// TCP SESSION CONSTRUCTOR
tcp_session::tcp_session(asio::ip::tcp::socket socket, message_handler& msg_handler, session_manager& session_mgr)
    : m_socket(std::move(socket)), m_message_handler(msg_handler), m_session_manager(session_mgr),
      m_session_id(session_mgr.create_session())
{
}

void tcp_session::start()
{
    // Register this session in session_manager for retry mechanism
    auto self = shared_from_this();
    m_session_manager.set_tcp_session(m_session_id, self);

    do_read();
}

void tcp_session::send(const std::string& data)
{
    do_write(data);
}

void tcp_session::do_read()
{
    auto self = shared_from_this();
    asio::async_read(m_socket, asio::buffer(m_data, BUFFER_SIZE),
                     [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
                         if (!ec)
                         {
                             if (bytes_transferred != BUFFER_SIZE)
                             {
                                 std::cerr << "\n\n[TCP] ERROR: Expected " << BUFFER_SIZE
                                           << " bytes but received " << bytes_transferred << std::endl;
                             }

                             m_data[BUFFER_SIZE - 1] = '\0';
                             std::cout << "\n\n[TCP] <- " << m_data.data() << std::endl;

                             process_received_data(bytes_transferred);
                             return;
                         }
                         if (ec != asio::error::operation_aborted)
                         {
                             std::cerr << "\n\n[TCP] ERROR: " << ec.message() << std::endl;
                         }
                         if (ec == asio::error::eof || ec == asio::error::connection_reset)
                         {
                             m_session_manager.remove_session(m_session_id);
                         }
                     });
}

void tcp_session::process_received_data(std::size_t bytes_transferred)
{
    std::string json_input(m_data.data());

    // First, deserialize to check if we need to send ACK
    message_t incoming_msg;
    if (deserialize_message_from_json(json_input.c_str(), &incoming_msg) == 0)
    {
        // Generate and send ACK immediately (if needed)
        message_t ack_msg;
        if (m_message_handler.generate_ack_if_needed(incoming_msg, m_session_id, &ack_msg))
        {
            char ack_json[BUFFER_SIZE];
            if (serialize_message_to_json(&ack_msg, ack_json) == 0)
            {
                std::string ack_str(ack_json);
                do_write(ack_str);
                std::cout << "[TCP] ACK sent immediately" << std::endl;
            }
            else
            {
                std::cerr << "\n\n[TCP] ERROR: Failed to serialize ACK message" << std::endl;
            }
        }
    }

    // Now process the message (can take time)
    message_processing_result result = m_message_handler.process_message(json_input, m_session_id);

    if (!result.success)
    {
        std::cerr << "\n\n[TCP] ERROR: Message processing failed - " << result.error_message << std::endl;
        if (!m_writing)
        {
            do_read();
        }
        return;
    }

    // Send additional response if needed
    if (result.should_send_response)
    {
        char response_json[BUFFER_SIZE];
        if (serialize_message_to_json(&result.response_message, response_json) == 0)
        {
            std::string response_str(response_json);
            do_write(response_str);
        }
        else
        {
            std::cerr << "\n\n[TCP] ERROR: Failed to serialize response message" << std::endl;
        }
    }

    // If nothing was queued, go back to reading
    if (!m_writing)
    {
        do_read();
    }
}

void tcp_session::do_write(const std::string& data)
{
    m_write_queue.push_back(data);
    if (!m_writing)
    {
        m_writing = true;
        do_write_next();
    }
}

void tcp_session::do_write_next()
{
    if (m_write_queue.empty())
    {
        m_writing = false;
        do_read();
        return;
    }

    auto self = shared_from_this();
    std::string data = m_write_queue.front();
    m_write_queue.pop_front();

    auto fixed_buffer = std::make_shared<std::array<char, BUFFER_SIZE>>();
    std::fill(fixed_buffer->begin(), fixed_buffer->end(), 0);

    if (data.length() >= BUFFER_SIZE)
    {
        std::cerr << "\n\n[TCP] ERROR: Message too large (" << data.length() << " bytes), truncating to "
                  << (BUFFER_SIZE - 1) << " bytes" << std::endl;
        std::copy_n(data.begin(), BUFFER_SIZE - 1, fixed_buffer->begin());
    }
    else
    {
        std::copy(data.begin(), data.end(), fixed_buffer->begin());
    }

    asio::async_write(m_socket, asio::buffer(*fixed_buffer, BUFFER_SIZE),
                      [this, self, fixed_buffer, data](const asio::error_code& ec, std::size_t bytes_transferred) {
                          if (!ec)
                          {
                              std::cout << "\n\n[TCP] -> " << data << std::endl;
                              do_write_next(); // Process next message in queue
                              return;
                          }
                          if (ec != asio::error::operation_aborted)
                          {
                              std::cerr << "\n\n[TCP] ERROR: " << ec.message() << std::endl;
                          }
                          m_writing = false;
                      });
}

// UDP SERVER CONSTRUCTOR
udp_server::udp_server(asio::io_context& io_context, const asio::ip::udp::endpoint& endpoint,
                       message_handler& msg_handler, session_manager& session_mgr)
    : m_socket(io_context), m_message_handler(msg_handler), m_session_manager(session_mgr)
{
    m_socket.open(endpoint.protocol());
    m_socket.set_option(asio::socket_base::reuse_address(true));
    if (endpoint.protocol() == asio::ip::udp::v6())
    {
        m_socket.set_option(asio::ip::v6_only(true));
    }
    m_socket.bind(endpoint);
    do_receive();
}

void udp_server::do_receive()
{
    m_socket.async_receive_from(
        asio::buffer(m_data), m_sender_endpoint, [this](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (!ec)
            {
                if (bytes_transferred >= BUFFER_SIZE)
                {
                    std::cerr << "\n\n[UDP] ERROR: Message too large! Received " << bytes_transferred
                              << " bytes, buffer is " << BUFFER_SIZE << " bytes" << std::endl;
                    do_receive();
                    return;
                }

                m_data[bytes_transferred] = '\0';

                std::string message_copy(m_data.data(), bytes_transferred);
                asio::ip::udp::endpoint sender_copy = m_sender_endpoint;

                std::cout << "\n\n[UDP] <- " << message_copy << std::endl;

                do_receive();

                process_received_data(message_copy, sender_copy);
                return;
            }
            if (ec != asio::error::operation_aborted)
            {
                std::cerr << "\n\n[UDP] ERROR: " << ec.message() << std::endl;
                do_receive();
            }
        });
}

void udp_server::process_received_data(const std::string& json_input, const asio::ip::udp::endpoint& sender_endpoint)
{
    if (json_input.empty())
    {
        std::cerr << "ERROR: Empty message received from " << sender_endpoint << std::endl;
        return;
    }

    // Get or create UDP session (deterministic session_id from endpoint)
    std::string session_id = m_session_manager.get_or_create_udp_session(sender_endpoint);

    if (!m_session_manager.is_authenticated(session_id))
    {
        auto session_info = m_session_manager.get_session_info(session_id);
        if (!session_info)
        {
            session_id = m_session_manager.create_session();
        }
    }

    // First, deserialize to check if we need to send ACK
    message_t incoming_msg;
    if (deserialize_message_from_json(json_input.c_str(), &incoming_msg) == 0)
    {
        // Generate and send ACK immediately (if needed)
        message_t ack_msg;
        if (m_message_handler.generate_ack_if_needed(incoming_msg, session_id, &ack_msg))
        {
            char ack_json[BUFFER_SIZE];
            if (serialize_message_to_json(&ack_msg, ack_json) == 0)
            {
                std::string ack_str(ack_json);
                do_send(ack_str, sender_endpoint);
                std::cout << "[UDP] ACK sent immediately" << std::endl;
            }
            else
            {
                std::cerr << "\n\n[UDP] ERROR: Failed to serialize ACK message" << std::endl;
            }
        }
    }

    // Now process the message (can take time)
    message_processing_result result = m_message_handler.process_message(json_input, session_id);

    if (!result.success)
    {
        std::cerr << "\n\n[UDP] ERROR: Message processing failed - " << result.error_message << std::endl;
        return;
    }

    // Send additional response if needed
    if (result.should_send_response)
    {
        char response_json[BUFFER_SIZE];
        if (serialize_message_to_json(&result.response_message, response_json) == 0)
        {
            std::string response_str(response_json);
            do_send(response_str, sender_endpoint);
        }
        else
        {
            std::cerr << "\n\n[UDP] ERROR: Failed to serialize response message" << std::endl;
        }
    }
}

void udp_server::do_send(const std::string& data, const asio::ip::udp::endpoint& target_endpoint)
{
    m_socket.async_send_to(
        asio::buffer(data.data(), data.length()), target_endpoint,
        [this, data, target_endpoint](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            auto addr = target_endpoint.address().to_string();
            auto port = target_endpoint.port();
            if (ec && ec != asio::error::operation_aborted)
            {
                std::cerr << "\n\n[UDP] ERROR: " << ec.message() << " -> " << addr << ":" << port << std::endl;
            }
            else
            {
                std::cout << "\n\n[UDP] -> " << addr << ":" << port << " | data=" << data << std::endl;
            }
        });
}

void udp_server::send_to_session(const std::string& session_id, const std::string& data)
{
    // Look up endpoint from session_manager
    auto endpoint = m_session_manager.get_udp_endpoint(session_id);
    if (endpoint)
    {
        do_send(data, *endpoint);
    }
    else
    {
        std::cerr << "[UDP] ERROR: No endpoint found for session: " << session_id << std::endl;
    }
}

server::server(asio::io_context& io_context, const config::server_config& config,
               std::unique_ptr<session_manager> session_mgr, std::unique_ptr<auth_module> auth_mod,
               std::unique_ptr<timer_manager> timer_mgr, std::unique_ptr<inventory_manager> inv_mgr)
    : m_io_context(io_context), m_config(config), m_tcp4_acceptor(io_context), m_tcp6_acceptor(io_context),
      m_session_manager(std::move(session_mgr)), m_auth_module(std::move(auth_mod)),
      m_timer_manager(std::move(timer_mgr)), m_inventory_manager(std::move(inv_mgr))
{
    // Create message_handler with generic send callback
    // The callback resolves whether to send via TCP or UDP
    m_message_handler =
        std::make_unique<message_handler>(*m_auth_module, *m_session_manager, *m_timer_manager, *m_inventory_manager,
                                          m_config.ack_timeout, m_config.max_retries,
                                          [this](const std::string& session_id, const std::string& data) {
                                              // Generic send callback - determines TCP vs UDP and sends appropriately
                                              auto session_info = m_session_manager->get_session_info(session_id);
                                              if (!session_info)
                                              {
                                                  std::cerr << "[SERVER] Cannot send to session " << session_id
                                                            << ": session not found" << std::endl;
                                                  return;
                                              }

                                              if (session_info->type == session_info::connection_type::UDP)
                                              {
                                                  if (session_info->udp_endpoint->address().is_v6())
                                                  {
                                                      m_udp_server_ipv6->send_to_session(session_id, data);
                                                  }
                                                  else
                                                  {
                                                      m_udp_server_ipv4->send_to_session(session_id, data);
                                                  }
                                              }
                                              else
                                              {
                                                  // For TCP, use weak_ptr from session_info
                                                  if (auto session = session_info->tcp_session_ref.lock())
                                                  {
                                                      session->send(data);
                                                  }
                                                  else
                                                  {
                                                      std::cerr << "[SERVER] WARNING: TCP session " << session_id
                                                                << " expired, cannot resend" << std::endl;
                                                  }
                                              }
                                          });

    configure_acceptor(m_tcp4_acceptor, make_tcp_endpoint(m_config.ip_v4, m_config.network_port));
    configure_acceptor(m_tcp6_acceptor, make_tcp_endpoint(m_config.ip_v6, m_config.network_port));

    m_udp_server_ipv4 = std::make_unique<udp_server>(
        io_context, make_udp_endpoint(m_config.ip_v4, m_config.network_port), *m_message_handler, *m_session_manager);
    m_udp_server_ipv6 = std::make_unique<udp_server>(
        io_context, make_udp_endpoint(m_config.ip_v6, m_config.network_port), *m_message_handler, *m_session_manager);
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
              << "  TCP IPv4: " << m_config.ip_v4 << ":" << m_config.network_port << '\n'
              << "  TCP IPv6: " << m_config.ip_v6 << ":" << m_config.network_port << '\n'
              << "  UDP IPv4: " << m_config.ip_v4 << ":" << m_config.network_port << '\n'
              << "  UDP IPv6: " << m_config.ip_v6 << ":" << m_config.network_port << '\n';
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
            try
            {
                std::cout << "Accepted TCP connection from " << socket.remote_endpoint() << '\n';
            }
            catch (const std::exception&)
            {
                std::cout << "Accepted TCP connection\n";
            }
            std::make_shared<tcp_session>(std::move(socket), *m_message_handler, *m_session_manager)->start();
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
    if (endpoint.protocol() == asio::ip::tcp::v6())
    {
        acceptor.set_option(asio::ip::v6_only(true));
    }
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
