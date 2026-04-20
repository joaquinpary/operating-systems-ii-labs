#include "server.hpp"
#include "mqtt_client.hpp"

#include <cerrno>
#include <common/json_manager.h>
#include <common/logger.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

tcp_session::tcp_session(int fd, event_loop& loop, shared_queue& shm, session_manager& session_mgr)
    : m_fd(fd), m_loop(loop), m_shm(shm), m_session_manager(session_mgr), m_session_id(session_mgr.create_session())
{
    m_read_buf.fill(0);
}

tcp_session::~tcp_session()
{
    close();
}

void tcp_session::notify_disconnect()
{
    if (!m_session_manager.is_authenticated(m_session_id))
    {
        return;
    }

    auto info = m_session_manager.get_session_info(m_session_id);
    if (!info || info->username.empty())
    {
        return;
    }

    request_slot_t req{};
    std::strncpy(req.session_id, m_session_id.c_str(), SESSION_ID_SIZE - 1);
    req.is_disconnect = true;
    req.is_authenticated = true;
    std::strncpy(req.username, info->username.c_str(), CREDENTIALS_SIZE - 1);
    std::strncpy(req.client_type, info->client_type.c_str(), ROLE_SIZE - 1);
    if (!m_shm.push_request(req))
    {
        LOG_WARNING_MSG("[TCP] IPC queue full, disconnect dropped user=%s sess=%s", info->username.c_str(),
                        m_session_id.c_str());
    }

    LOG_INFO_MSG("[TCP] disconnect user=%s sess=%s", info->username.c_str(), m_session_id.c_str());
}

void tcp_session::start()
{
    auto self = shared_from_this();
    m_session_manager.set_tcp_session(m_session_id, self);

    m_loop.add_fd(m_fd, EPOLLIN | EPOLLET, [this](std::uint32_t events) {
        if (events & (EPOLLHUP | EPOLLERR))
        {
            notify_disconnect();
            m_session_manager.remove_session(m_session_id);
            close();
            return;
        }
        if (events & EPOLLIN)
        {
            on_readable();
        }
        if (events & EPOLLOUT)
        {
            on_writable();
        }
    });
}

void tcp_session::send(const std::string& data)
{
    m_write_queue.push_back(data);
    if (!m_writing)
    {
        m_writing = true;
        m_loop.modify_fd(m_fd, EPOLLIN | EPOLLOUT | EPOLLET);
        do_write_next();
    }
}

void tcp_session::close()
{
    if (m_closed)
    {
        return;
    }
    m_closed = true;
    m_loop.remove_fd(m_fd);
    ::close(m_fd);
    m_fd = -1;
}

void tcp_session::on_readable()
{
    while (true)
    {
        ssize_t remaining = static_cast<ssize_t>(BUFFER_SIZE) - static_cast<ssize_t>(m_read_offset);
        if (remaining <= 0)
        {
            m_read_buf[BUFFER_SIZE - 1] = '\0';
            std::string json_input(m_read_buf.data());
            LOG_INFO_MSG("[TCP] <- sess=%s len=%zu", m_session_id.c_str(), m_read_offset);

            request_slot_t req{};
            std::strncpy(req.session_id, m_session_id.c_str(), SESSION_ID_SIZE - 1);
            req.protocol = static_cast<std::uint8_t>(protocol_type::TCP);
            std::memcpy(req.raw_json, m_read_buf.data(), BUFFER_SIZE);
            req.payload_len = BUFFER_SIZE;
            req.is_authenticated = m_session_manager.is_authenticated(m_session_id);
            req.is_blacklisted = m_session_manager.is_blacklisted(m_session_id);

            auto client_type = m_session_manager.get_client_type(m_session_id);
            std::strncpy(req.client_type, client_type.c_str(), ROLE_SIZE - 1);

            auto info = m_session_manager.get_session_info(m_session_id);
            if (info)
            {
                std::strncpy(req.username, info->username.c_str(), CREDENTIALS_SIZE - 1);
            }

            if (!m_shm.push_request(req))
            {
                LOG_WARNING_MSG("[TCP] IPC queue full, dropping message sess=%s", m_session_id.c_str());
            }

            m_read_offset = 0;
            m_read_buf.fill(0);
            continue;
        }

        ssize_t n = recv(m_fd, m_read_buf.data() + m_read_offset, static_cast<std::size_t>(remaining), 0);
        if (n > 0)
        {
            m_read_offset += static_cast<std::size_t>(n);
        }
        else if (n == 0)
        {
            LOG_INFO_MSG("[TCP] closed by peer sess=%s", m_session_id.c_str());
            notify_disconnect();
            m_session_manager.remove_session(m_session_id);
            close();
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            std::cerr << "[TCP] recv err=" << strerror(errno) << '\n';
            LOG_ERROR_MSG("[TCP] recv err=%s sess=%s", strerror(errno), m_session_id.c_str());
            notify_disconnect();
            m_session_manager.remove_session(m_session_id);
            close();
            return;
        }
    }
}

void tcp_session::do_write_next()
{
    if (m_write_queue.empty())
    {
        m_writing = false;
        m_loop.modify_fd(m_fd, EPOLLIN | EPOLLET);
        return;
    }

    std::string data = m_write_queue.front();
    m_write_queue.pop_front();

    std::array<char, BUFFER_SIZE> buf{};
    buf.fill(0);
    if (data.length() >= BUFFER_SIZE)
    {
        std::copy_n(data.begin(), BUFFER_SIZE - 1, buf.begin());
    }
    else
    {
        std::copy(data.begin(), data.end(), buf.begin());
    }

    std::size_t total_sent = 0;
    while (total_sent < BUFFER_SIZE)
    {
        ssize_t n = ::send(m_fd, buf.data() + total_sent, BUFFER_SIZE - total_sent, MSG_NOSIGNAL);
        if (n > 0)
        {
            total_sent += static_cast<std::size_t>(n);
        }
        else if (n == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            std::cerr << "[TCP] send err=" << strerror(errno) << '\n';
            LOG_ERROR_MSG("[TCP] send err=%s sess=%s", strerror(errno), m_session_id.c_str());
            m_writing = false;
            return;
        }
    }

    if (total_sent == BUFFER_SIZE)
    {
        LOG_INFO_MSG("[TCP] -> sess=%s len=%zu", m_session_id.c_str(), data.size());
        do_write_next();
    }
}

void tcp_session::on_writable()
{
    if (m_writing)
    {
        do_write_next();
    }
}

udp_server::udp_server(event_loop& loop, shared_queue& shm, session_manager& session_mgr,
                       const posix_address& bind_addr)
    : m_fd(-1), m_loop(loop), m_shm(shm), m_session_manager(session_mgr)
{
    int domain = (bind_addr.family() == AF_INET6) ? AF_INET6 : AF_INET;
    m_fd = socket(domain, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (m_fd == -1)
    {
        throw std::runtime_error(std::string("UDP socket() failed: ") + strerror(errno));
    }

    int optval = 1;
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (domain == AF_INET6)
    {
        int v6only = 1;
        setsockopt(m_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    if (bind(m_fd, bind_addr.sockaddr_ptr(), bind_addr.sockaddr_len()) == -1)
    {
        ::close(m_fd);
        throw std::runtime_error(std::string("UDP bind() failed: ") + strerror(errno));
    }

    m_loop.add_fd(m_fd, EPOLLIN, [this](std::uint32_t /*events*/) { on_readable(); });
}

udp_server::~udp_server()
{
    if (m_fd != -1)
    {
        m_loop.remove_fd(m_fd);
        ::close(m_fd);
    }
}

void udp_server::on_readable()
{
    while (true)
    {
        struct sockaddr_storage sender_storage
        {
        };
        socklen_t sender_len = sizeof(sender_storage);

        ssize_t n = recvfrom(m_fd, m_read_buf.data(), BUFFER_SIZE - 1, 0,
                             reinterpret_cast<struct sockaddr*>(&sender_storage), &sender_len);
        if (n <= 0)
        {
            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                break;
            }
            if (n == -1)
            {
                std::cerr << "[UDP] recvfrom error: " << strerror(errno) << std::endl;
            }
            break;
        }

        m_read_buf[static_cast<std::size_t>(n)] = '\0';
        std::string json_input(m_read_buf.data(), static_cast<std::size_t>(n));
        posix_address sender_addr(sender_storage, sender_len);

        LOG_INFO_MSG("[UDP] <- from=%s len=%zd", sender_addr.to_string().c_str(), n);

        std::string session_id = m_session_manager.get_or_create_udp_session(sender_addr);

        request_slot_t req{};
        std::strncpy(req.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
        req.protocol = static_cast<std::uint8_t>(protocol_type::UDP);
        std::memcpy(req.raw_json, m_read_buf.data(), BUFFER_SIZE);
        req.payload_len = static_cast<std::uint32_t>(n);
        req.is_authenticated = m_session_manager.is_authenticated(session_id);
        req.is_blacklisted = m_session_manager.is_blacklisted(session_id);

        auto client_type = m_session_manager.get_client_type(session_id);
        std::strncpy(req.client_type, client_type.c_str(), ROLE_SIZE - 1);

        auto info = m_session_manager.get_session_info(session_id);
        if (info)
        {
            std::strncpy(req.username, info->username.c_str(), CREDENTIALS_SIZE - 1);
        }

        if (!m_shm.push_request(req))
        {
            LOG_WARNING_MSG("[UDP] IPC queue full, dropping message from=%s", sender_addr.to_string().c_str());
        }
    }
}

void udp_server::send_to_session(const std::string& session_id, const std::string& data)
{
    auto endpoint = m_session_manager.get_udp_endpoint(session_id);
    if (!endpoint)
    {
        std::cerr << "[UDP] no endpoint sess=" << session_id << '\n';
        LOG_ERROR_MSG("[UDP] no endpoint sess=%s", session_id.c_str());
        return;
    }

    std::array<char, BUFFER_SIZE> buf{};
    buf.fill(0);
    std::size_t copy_len = std::min(data.length(), static_cast<std::size_t>(BUFFER_SIZE - 1));
    std::copy_n(data.begin(), copy_len, buf.begin());

    ssize_t sent = sendto(m_fd, buf.data(), BUFFER_SIZE, 0, endpoint->sockaddr_ptr(), endpoint->sockaddr_len());
    if (sent == -1)
    {
        std::cerr << "[UDP] sendto err=" << strerror(errno) << '\n';
        LOG_ERROR_MSG("[UDP] sendto err=%s sess=%s", strerror(errno), session_id.c_str());
    }
    else
    {
        LOG_INFO_MSG("[UDP] -> sess=%s to=%s len=%zu", session_id.c_str(), endpoint->to_string().c_str(), copy_len);
    }
}

server::server(event_loop& loop, shared_queue& shm, int response_efd, const config::server_config& config,
               std::unique_ptr<session_manager> session_mgr, std::unique_ptr<timer_manager> timer_mgr)
    : m_loop(loop), m_shm(shm), m_response_efd(response_efd), m_config(config),
      m_session_manager(std::move(session_mgr)), m_timer_manager(std::move(timer_mgr))
{
}

server::~server()
{
    stop();
}

void server::start()
{
    posix_address tcp4_addr = posix_address::from_ipv4(m_config.ip_v4, m_config.network_port);
    posix_address tcp6_addr = posix_address::from_ipv6(m_config.ip_v6, m_config.network_port);

    m_tcp4_fd = create_listen_socket(tcp4_addr);
    m_tcp6_fd = create_listen_socket(tcp6_addr);

    m_loop.add_fd(m_tcp4_fd, EPOLLIN, [this](std::uint32_t /*events*/) { on_accept(m_tcp4_fd); });
    m_loop.add_fd(m_tcp6_fd, EPOLLIN, [this](std::uint32_t /*events*/) { on_accept(m_tcp6_fd); });

    posix_address udp4_addr = posix_address::from_ipv4(m_config.ip_v4, m_config.network_port);
    posix_address udp6_addr = posix_address::from_ipv6(m_config.ip_v6, m_config.network_port);

    m_udp_server_ipv4 = std::make_unique<udp_server>(m_loop, m_shm, *m_session_manager, udp4_addr);
    m_udp_server_ipv6 = std::make_unique<udp_server>(m_loop, m_shm, *m_session_manager, udp6_addr);

    m_loop.add_fd(m_response_efd, EPOLLIN, [this](std::uint32_t /*events*/) { on_response_ready(); });

    LOG_INFO_MSG("[SERVER] started tcp4=%s tcp6=%s udp port=%u", m_config.ip_v4.c_str(), m_config.ip_v6.c_str(),
                 m_config.network_port);
}

void server::stop()
{
    if (m_tcp4_fd != -1)
    {
        m_loop.remove_fd(m_tcp4_fd);
        ::close(m_tcp4_fd);
        m_tcp4_fd = -1;
    }
    if (m_tcp6_fd != -1)
    {
        m_loop.remove_fd(m_tcp6_fd);
        ::close(m_tcp6_fd);
        m_tcp6_fd = -1;
    }

    m_udp_server_ipv4.reset();
    m_udp_server_ipv6.reset();

    for (auto& [fd, session] : m_tcp_sessions)
    {
        session->close();
    }
    m_tcp_sessions.clear();
}

int server::create_listen_socket(const posix_address& addr)
{
    int domain = (addr.family() == AF_INET6) ? AF_INET6 : AF_INET;
    int fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd == -1)
    {
        throw std::runtime_error(std::string("TCP socket() failed: ") + strerror(errno));
    }

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (domain == AF_INET6)
    {
        int v6only = 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    if (::bind(fd, addr.sockaddr_ptr(), addr.sockaddr_len()) == -1)
    {
        ::close(fd);
        throw std::runtime_error(std::string("TCP bind() failed: ") + strerror(errno));
    }

    if (listen(fd, SOMAXCONN) == -1)
    {
        ::close(fd);
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
    }

    return fd;
}

void server::on_accept(int listen_fd)
{
    while (true)
    {
        struct sockaddr_storage client_addr
        {
        };
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept4(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len, SOCK_NONBLOCK);
        if (client_fd == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            std::cerr << "[SERVER] accept4 err=" << strerror(errno) << '\n';
            LOG_ERROR_MSG("[SERVER] accept4 err=%s", strerror(errno));
            break;
        }

        posix_address peer(client_addr, addr_len);

        auto session = std::make_shared<tcp_session>(client_fd, m_loop, m_shm, *m_session_manager);
        LOG_INFO_MSG("[TCP] connect from=%s sess=%s", peer.to_string().c_str(), session->session_id().c_str());

        m_tcp_sessions[client_fd] = session;
        session->start();
    }
}

void server::on_response_ready()
{
    std::uint64_t val;
    if (read(m_response_efd, &val, sizeof(val)) == -1)
    {
        if (errno != EAGAIN)
        {
            std::cerr << "[SERVER] eventfd read err=" << strerror(errno) << '\n';
            LOG_ERROR_MSG("[SERVER] eventfd read err=%s", strerror(errno));
        }
    }

    response_slot_t resp;
    while (m_shm.pop_response(resp))
    {
        dispatch_response(resp);
    }
}

void server::dispatch_response(const response_slot_t& resp)
{
    std::string session_id(resp.session_id);
    auto cmd = static_cast<response_command>(resp.command);

    if (!session_id.empty() && cmd != response_command::MARK_AUTHENTICATED)
    {
        if (!m_session_manager->has_session(session_id))
        {
            return;
        }
    }

    switch (cmd)
    {
    case response_command::SEND: {
        std::string data(resp.payload, resp.payload_len);
        std::string target_session = session_id;
        if (target_session.empty() && resp.target_username[0] != '\0')
        {
            target_session = m_session_manager->find_session_by_username(std::string(resp.target_username));
            if (target_session.empty())
            {
                std::cerr << "[REACTOR] no session for user=" << resp.target_username << '\n';
                LOG_ERROR_MSG("[DISPATCH] SEND no session for user=%s", resp.target_username);
                break;
            }
        }
        send_to_session(target_session, data);
        LOG_DEBUG_MSG("[DISPATCH] SEND sess=%s len=%u", target_session.c_str(), resp.payload_len);

        if (resp.start_ack_timer && resp.timer_timeout > 0)
        {
            std::string timer_key(resp.timer_key);
            std::string payload_copy(resp.payload, resp.payload_len);
            std::uint32_t retry_count = resp.retry_count;
            std::uint32_t max_retries = resp.max_retries;
            std::uint32_t timeout = resp.timer_timeout;

            m_timer_manager->start_ack_timer(
                target_session, timer_key, static_cast<int>(timeout),
                [this, target_session, timer_key, payload_copy, retry_count, max_retries]() {
                    handle_ack_timeout(target_session, timer_key, payload_copy, retry_count, max_retries);
                });
            LOG_INFO_MSG("[DISPATCH] SEND+ACK_TIMER sess=%s ts=%s timeout=%u", target_session.c_str(),
                         timer_key.c_str(), timeout);
        }
        break;
    }
    case response_command::START_ACK_TIMER: {
        std::string timer_key(resp.timer_key);
        std::string payload(resp.payload, resp.payload_len);
        std::uint32_t retry_count = resp.retry_count;
        std::uint32_t max_retries = resp.max_retries;
        std::uint32_t timeout = resp.timer_timeout;

        m_timer_manager->start_ack_timer(session_id, timer_key, static_cast<int>(timeout),
                                         [this, session_id, timer_key, payload, retry_count, max_retries]() {
                                             handle_ack_timeout(session_id, timer_key, payload, retry_count,
                                                                max_retries);
                                         });
        break;
    }
    case response_command::CANCEL_ACK_TIMER: {
        std::string timer_key(resp.timer_key);
        if (m_timer_manager->cancel_ack_timer(session_id, timer_key))
        {
            LOG_DEBUG_MSG("[DISPATCH] -ACK sess=%s ts=%s", session_id.c_str(), timer_key.c_str());
        }
        break;
    }
    case response_command::CLEAR_TIMERS: {
        m_timer_manager->clear_session_timers(session_id);
        break;
    }
    case response_command::BLACKLIST: {
        m_session_manager->blacklist_session(session_id);
        break;
    }
    case response_command::MARK_AUTHENTICATED: {
        std::string client_type(resp.client_type);
        std::string username(resp.username);
        m_session_manager->mark_authenticated(session_id, client_type, username);
        break;
    }
    case response_command::START_KEEPALIVE_TIMER: {
        std::uint32_t timeout = resp.timer_timeout;
        m_timer_manager->start_keepalive_timer(session_id, static_cast<int>(timeout),
                                               [this, session_id]() { handle_keepalive_timeout(session_id); });
        break;
    }
    case response_command::RESET_KEEPALIVE_TIMER: {
        m_timer_manager->reset_keepalive_timer(session_id);
        break;
    }
    case response_command::DISCONNECT: {
        handle_keepalive_timeout(session_id);
        break;
    }
    case response_command::BROADCAST: {
        std::string data(resp.payload, resp.payload_len);
        auto sessions = std::make_shared<std::vector<std::string>>(m_session_manager->get_authenticated_sessions());
        bool need_ack = resp.start_ack_timer && resp.timer_timeout > 0;
        std::string timer_key(resp.timer_key);
        std::uint32_t retry_count = resp.retry_count;
        std::uint32_t max_retries = resp.max_retries;
        std::uint32_t timeout = resp.timer_timeout;

        LOG_INFO_MSG("[DISPATCH] BROADCAST len=%u to %zu sessions (batched)", resp.payload_len, sessions->size());

        static constexpr std::size_t BROADCAST_BATCH_SIZE = 100;

        auto send_batch = [this, sessions, data, need_ack, timer_key, retry_count, max_retries,
                           timeout](std::size_t start, std::size_t end) {
            for (std::size_t i = start; i < end; ++i)
            {
                const auto& target_session = (*sessions)[i];
                if (!m_session_manager->has_session(target_session))
                    continue;
                send_to_session(target_session, data);

                if (need_ack && m_session_manager->get_client_type(target_session) != CLI)
                {
                    std::string payload_copy = data;
                    m_timer_manager->start_ack_timer(
                        target_session, timer_key, static_cast<int>(timeout),
                        [this, target_session, timer_key, payload_copy, retry_count, max_retries]() {
                            handle_ack_timeout(target_session, timer_key, payload_copy, retry_count, max_retries);
                        });
                }
            }
        };

        std::size_t total = sessions->size();
        std::size_t first_end = std::min(BROADCAST_BATCH_SIZE, total);
        send_batch(0, first_end);

        for (std::size_t offset = first_end; offset < total; offset += BROADCAST_BATCH_SIZE)
        {
            std::size_t batch_end = std::min(offset + BROADCAST_BATCH_SIZE, total);
            m_loop.post([send_batch, offset, batch_end]() { send_batch(offset, batch_end); });
        }
        break;
    }
    case response_command::MQTT_PUBLISH_ROUTE: {
        if (m_mqtt)
        {
            m_mqtt->assign_route(resp.mqtt_transaction_id);
        }
        break;
    }
    }
}

void server::handle_keepalive_timeout(const std::string& session_id)
{
    LOG_WARNING_MSG("[KEEPALIVE_TIMEOUT] sess=%s disconnecting", session_id.c_str());
    m_timer_manager->clear_session_timers(session_id);

    auto info = m_session_manager->get_session_info(session_id);
    if (!info)
    {
        std::cerr << "[REACTOR] sess=" << session_id << " not found\n";
        LOG_ERROR_MSG("[KEEPALIVE_TIMEOUT] sess=%s not found", session_id.c_str());
        return;
    }

    if (info->type == session_info::connection_type::TCP)
    {
        if (auto tcp_sess = info->tcp_session_ref.lock())
        {
            tcp_sess->notify_disconnect();
            tcp_sess->close();
        }
    }
    else if (!info->username.empty())
    {
        request_slot_t req{};
        std::strncpy(req.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
        req.is_disconnect = true;
        req.is_authenticated = true;
        std::strncpy(req.username, info->username.c_str(), CREDENTIALS_SIZE - 1);
        std::strncpy(req.client_type, info->client_type.c_str(), ROLE_SIZE - 1);
        if (!m_shm.push_request(req))
        {
            LOG_WARNING_MSG("[UDP] IPC queue full, disconnect dropped user=%s sess=%s", info->username.c_str(),
                            session_id.c_str());
        }
        LOG_INFO_MSG("[UDP] disconnect user=%s sess=%s", info->username.c_str(), session_id.c_str());
    }

    m_session_manager->remove_session(session_id);
}

void server::handle_ack_timeout(const std::string& session_id, const std::string& timer_key, const std::string& payload,
                                std::uint32_t retry_count, std::uint32_t max_retries)
{
    if (!m_session_manager->has_session(session_id))
    {
        return;
    }

    if (retry_count >= max_retries - 1)
    {
        LOG_WARNING_MSG("[ACK_TIMEOUT] MAX RETRIES sess=%s ts=%s retries=%u", session_id.c_str(), timer_key.c_str(),
                        max_retries);
        m_timer_manager->clear_session_timers(session_id);

        auto info = m_session_manager->get_session_info(session_id);
        if (info && info->type == session_info::connection_type::TCP)
        {
            if (auto tcp_sess = info->tcp_session_ref.lock())
            {
                tcp_sess->notify_disconnect();
                tcp_sess->close();
            }
            m_session_manager->remove_session(session_id);
        }
        else
        {
            if (info && !info->username.empty())
            {
                request_slot_t req{};
                std::strncpy(req.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
                req.is_disconnect = true;
                req.is_authenticated = true;
                std::strncpy(req.username, info->username.c_str(), CREDENTIALS_SIZE - 1);
                std::strncpy(req.client_type, info->client_type.c_str(), ROLE_SIZE - 1);
                if (!m_shm.push_request(req))
                {
                    LOG_WARNING_MSG("[UDP] IPC queue full, disconnect dropped user=%s sess=%s", info->username.c_str(),
                                    session_id.c_str());
                }
            }
            m_session_manager->blacklist_session(session_id);
        }
        return;
    }

    LOG_WARNING_MSG("[ACK_TIMEOUT] sess=%s ts=%s retry=%u/%u resending", session_id.c_str(), timer_key.c_str(),
                    retry_count + 1, max_retries);
    send_to_session(session_id, payload);

    m_timer_manager->start_ack_timer(session_id, timer_key, static_cast<int>(m_config.ack_timeout),
                                     [this, session_id, timer_key, payload, retry_count, max_retries]() {
                                         handle_ack_timeout(session_id, timer_key, payload, retry_count + 1,
                                                            max_retries);
                                     });
}

void server::send_to_session(const std::string& session_id, const std::string& data)
{
    auto info = m_session_manager->get_session_info(session_id);
    if (!info)
    {
        std::cerr << "[SERVER] send fail sess=" << session_id << " not found\n";
        LOG_ERROR_MSG("[SERVER] send fail sess=%s not found", session_id.c_str());
        return;
    }

    if (info->type == session_info::connection_type::UDP)
    {
        auto endpoint = m_session_manager->get_udp_endpoint(session_id);
        if (endpoint && endpoint->is_v6())
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
        if (auto tcp_sess = info->tcp_session_ref.lock())
        {
            tcp_sess->send(data);
        }
        else
        {
            std::cerr << "[SERVER] TCP sess=" << session_id << " expired\n";
            LOG_WARNING_MSG("[SERVER] TCP sess=%s expired (weak_ptr)", session_id.c_str());
        }
    }
}
