#include "session_manager.hpp"

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
    oss << "session_" << session_num;
    std::string session_id = oss.str();

    session_info info;
    info.session_id = session_id;
    info.is_authenticated = false;
    info.client_type = "";
    info.username = "";

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
