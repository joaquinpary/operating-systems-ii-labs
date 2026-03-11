#ifndef SERVER_HPP
#define SERVER_HPP

#include "config.hpp"
#include "event_loop.hpp"
#include "ipc.hpp"
#include "posix_address.hpp"
#include "session_manager.hpp"
#include "timer_manager.hpp"
#include <common/json_manager.h>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

/**
 * A TCP session managed by the reactor.
 * Owns a non-blocking socket fd. All I/O happens in the reactor thread.
 */
class tcp_session : public std::enable_shared_from_this<tcp_session>
{
  public:
    tcp_session(int fd, event_loop& loop, shared_queue& shm, session_manager& session_mgr);
    ~tcp_session();

    void start();
    void send(const std::string& data);
    void close();
    void notify_disconnect();

    const std::string& session_id() const
    {
        return m_session_id;
    }
    int fd() const
    {
        return m_fd;
    }

  private:
    void on_readable();
    void do_write_next();
    void on_writable();

    int m_fd;
    event_loop& m_loop;
    shared_queue& m_shm;
    session_manager& m_session_manager;
    std::string m_session_id;

    std::array<char, BUFFER_SIZE> m_read_buf;
    std::size_t m_read_offset = 0;

    std::deque<std::string> m_write_queue;
    bool m_writing = false;
    bool m_closed = false;
};

/**
 * A UDP server socket: one per address family (IPv4 / IPv6).
 * All I/O in the reactor thread.
 */
class udp_server
{
  public:
    udp_server(event_loop& loop, shared_queue& shm, session_manager& session_mgr, const posix_address& bind_addr);
    ~udp_server();

    void send_to_session(const std::string& session_id, const std::string& data);
    int fd() const
    {
        return m_fd;
    }

  private:
    void on_readable();

    int m_fd;
    event_loop& m_loop;
    shared_queue& m_shm;
    session_manager& m_session_manager;
    std::array<char, BUFFER_SIZE> m_read_buf;
};

/**
 * The main server class — runs in the reactor process.
 * Owns TCP acceptors, UDP servers, session manager, timer manager,
 * and the reactor-side of the shared queue.
 */
class server
{
  public:
    server(event_loop& loop, shared_queue& shm, int response_efd, const config::server_config& config,
           std::unique_ptr<session_manager> session_mgr, std::unique_ptr<timer_manager> timer_mgr);
    ~server();

    void start();
    void stop();

  private:
    // TCP acceptor setup & callbacks
    int create_listen_socket(const posix_address& addr);
    void on_accept(int listen_fd);

    // Response dispatch (worker → reactor)
    void on_response_ready();
    void dispatch_response(const response_slot_t& resp);

    // Timer expiry handlers
    void handle_ack_timeout(const std::string& session_id, const std::string& timer_key, const std::string& payload,
                            std::uint32_t retry_count, std::uint32_t max_retries);
    void handle_keepalive_timeout(const std::string& session_id);

    // Send callback (used by timer retry logic in reactor)
    void send_to_session(const std::string& session_id, const std::string& data);

    event_loop& m_loop;
    shared_queue& m_shm;
    int m_response_efd; // eventfd for worker→reactor notification
    config::server_config m_config;

    // TCP listen sockets (IPv4 + IPv6)
    int m_tcp4_fd = -1;
    int m_tcp6_fd = -1;

    // UDP servers
    std::unique_ptr<udp_server> m_udp_server_ipv4;
    std::unique_ptr<udp_server> m_udp_server_ipv6;

    // Active TCP sessions: fd → session
    std::unordered_map<int, std::shared_ptr<tcp_session>> m_tcp_sessions;

    // Server modules (reactor-side only)
    std::unique_ptr<session_manager> m_session_manager;
    std::unique_ptr<timer_manager> m_timer_manager;
};

#endif // SERVER_HPP
