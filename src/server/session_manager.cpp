#include "session_manager.hpp"

#include <iostream>
#include <sstream>

session_manager::session_manager() : m_session_counter(0)
{
}

session_manager::~session_manager()
{
    m_sessions.clear();
}

std::string session_manager::create_session()
{
    std::uint64_t session_num = ++m_session_counter;
    std::ostringstream oss;
    oss << TCP_SESSION_PREFIX << session_num;
    std::string session_id = oss.str();

    session_info info;
    info.session_id = session_id;
    info.is_authenticated = false;
    info.client_type = "";
    info.username = "";
    info.type = session_info::connection_type::TCP;

    m_sessions[session_id] = info;
    return session_id;
}

std::string session_manager::get_or_create_udp_session(const posix_address& endpoint)
{
    std::string session_id = make_udp_session_id(endpoint);

    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.udp_endpoint = endpoint;
        return session_id;
    }

    session_info info;
    info.session_id = session_id;
    info.is_authenticated = false;
    info.client_type = "";
    info.username = "";
    info.type = session_info::connection_type::UDP;
    info.udp_endpoint = endpoint;

    m_sessions[session_id] = info;
    return session_id;
}

void session_manager::mark_authenticated(const std::string& session_id, const std::string& client_type,
                                         const std::string& username)
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.is_authenticated = true;
        it->second.client_type = client_type;
        it->second.username = username;
    }
}

bool session_manager::is_authenticated(const std::string& session_id) const
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.is_authenticated;
    }
    return false;
}

bool session_manager::is_blacklisted(const std::string& session_id) const
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.is_blacklisted;
    }
    return false;
}

void session_manager::blacklist_session(const std::string& session_id)
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.is_blacklisted = true;
        std::cout << "[SESSION] Blacklisted session: " << session_id << std::endl;
    }
}

std::unique_ptr<session_info> session_manager::get_session_info(const std::string& session_id) const
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        auto info = std::make_unique<session_info>();
        *info = it->second;
        return info;
    }
    return nullptr;
}

void session_manager::remove_session(const std::string& session_id)
{
    m_sessions.erase(session_id);
}

std::string session_manager::get_client_type(const std::string& session_id) const
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.client_type;
    }
    return "";
}

std::string session_manager::find_session_by_username(const std::string& username) const
{
    for (const auto& [session_id, info] : m_sessions)
    {
        if (info.is_authenticated && !info.is_blacklisted && info.username == username)
        {
            return session_id;
        }
    }
    return "";
}

std::optional<posix_address> session_manager::get_udp_endpoint(const std::string& session_id) const
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.udp_endpoint;
    }
    return std::nullopt;
}

std::string session_manager::make_udp_session_id(const posix_address& endpoint) const
{
    std::ostringstream oss;
    oss << UDP_SESSION_PREFIX << endpoint.ip_string() << UDP_SESSION_SEPARATOR << endpoint.port();
    return oss.str();
}

void session_manager::set_tcp_session(const std::string& session_id, std::weak_ptr<tcp_session> session)
{
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.tcp_session_ref = session;
    }
}
