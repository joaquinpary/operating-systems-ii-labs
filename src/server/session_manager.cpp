#include "session_manager.hpp"

#include <iostream>
#include <sstream>

// SESSION MANAGER CONSTRUCTOR
session_manager::session_manager() : m_session_counter(0)
{
}

session_manager::~session_manager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.clear();
}

std::string session_manager::create_session()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::uint64_t session_num = ++m_session_counter;
    std::ostringstream oss;
    oss << "tcp_session_" << session_num;
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

std::string session_manager::get_or_create_udp_session(const asio::ip::udp::endpoint& endpoint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Generate deterministic session_id from endpoint
    std::string session_id = make_udp_session_id(endpoint);
    
    // Check if session already exists
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        // Update endpoint (in case it changed - NAT, reconnection)
        it->second.udp_endpoint = endpoint;
        return session_id;
    }
    
    // Create new UDP session
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
    std::lock_guard<std::mutex> lock(m_mutex);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.is_authenticated;
    }
    return false;
}

bool session_manager::is_blacklisted(const std::string& session_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.is_blacklisted;
    }
    return false;
}

void session_manager::blacklist_session(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.is_blacklisted = true;
        std::cout << "[SESSION] Blacklisted session: " << session_id << std::endl;
    }
}

std::unique_ptr<session_info> session_manager::get_session_info(const std::string& session_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(session_id);
}

std::string session_manager::get_client_type(const std::string& session_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.client_type;
    }
    return "";
}

// ==================== UDP-SPECIFIC METHODS ====================

std::optional<asio::ip::udp::endpoint> session_manager::get_udp_endpoint(const std::string& session_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        return it->second.udp_endpoint;
    }
    return std::nullopt;
}

std::string session_manager::make_udp_session_id(const asio::ip::udp::endpoint& endpoint) const
{
    std::ostringstream oss;
    oss << "udp_" << endpoint.address().to_string() << "_" << endpoint.port();
    return oss.str();
}

void session_manager::set_tcp_session(const std::string& session_id, std::weak_ptr<tcp_session> session)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_sessions.find(session_id);
    if (it != m_sessions.end())
    {
        it->second.tcp_session_ref = session;
    }
}
