#include "timer_manager.hpp"
#include <iostream>

timer_manager::timer_manager(asio::io_context& io_context)
    : m_io_context(io_context)
{
    std::cout << "[TIMER_MANAGER] Initialized" << std::endl;
}

timer_manager::~timer_manager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Cancel all timers
    for (auto& session_pair : m_ack_timers)
    {
        for (auto& timer_pair : session_pair.second)
        {
            if (timer_pair.second)
            {
                timer_pair.second->cancel();
            }
        }
    }
    m_ack_timers.clear();
    
    for (auto& timer_pair : m_keepalive_timers)
    {
        if (timer_pair.second)
        {
            timer_pair.second->cancel();
        }
    }
    m_keepalive_timers.clear();
    
    std::cout << "[TIMER_MANAGER] Destroyed" << std::endl;
}

// ==================== ACK TIMERS ====================

void timer_manager::start_ack_timer(const std::string& session_id, 
                                    const std::string& msg_timestamp,
                                    int timeout_seconds,
                                    std::function<void()> on_timeout)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Create timer
    auto timer = std::make_shared<asio::steady_timer>(m_io_context);
    timer->expires_after(std::chrono::seconds(timeout_seconds));
    
    // Set async wait with callback
    timer->async_wait([on_timeout, session_id, msg_timestamp](const asio::error_code& ec) {
        if (!ec)  // Timer expired (not cancelled)
        {
            std::cout << "[TIMER] ACK timeout for session: " << session_id 
                      << ", message: " << msg_timestamp << std::endl;
            on_timeout();
        }
        else if (ec == asio::error::operation_aborted)
        {
            std::cout << "[TIMER] ACK timer cancelled for session: " << session_id 
                      << ", message: " << msg_timestamp << std::endl;
        }
    });
    
    // Store timer
    m_ack_timers[session_id][msg_timestamp] = timer;
    
    std::cout << "[TIMER] Started ACK timer for session: " << session_id 
              << ", message: " << msg_timestamp 
              << " (timeout: " << timeout_seconds << "s)" << std::endl;
}

bool timer_manager::cancel_ack_timer(const std::string& session_id, const std::string& msg_timestamp)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto session_it = m_ack_timers.find(session_id);
    if (session_it == m_ack_timers.end())
    {
        return false;
    }
    
    auto timer_it = session_it->second.find(msg_timestamp);
    if (timer_it == session_it->second.end())
    {
        return false;
    }
    
    if (timer_it->second)
    {
        timer_it->second->cancel();
    }
    
    session_it->second.erase(timer_it);
    
    // Clean up session map if empty
    if (session_it->second.empty())
    {
        m_ack_timers.erase(session_it);
    }
    
    std::cout << "[TIMER] Cancelled ACK timer for session: " << session_id 
              << ", message: " << msg_timestamp << std::endl;
    return true;
}

// ==================== KEEPALIVE TIMERS (PLACEHOLDER) ====================

void timer_manager::start_keepalive_timer(const std::string& session_id, int timeout_seconds, std::function<void()> on_timeout)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Cancel existing timer if any
    auto it = m_keepalive_timers.find(session_id);
    if (it != m_keepalive_timers.end() && it->second)
    {
        it->second->cancel();
    }
    
    // Create new timer
    auto timer = std::make_shared<asio::steady_timer>(m_io_context);
    timer->expires_after(std::chrono::seconds(timeout_seconds));
    
    timer->async_wait([on_timeout, session_id](const asio::error_code& ec) {
        if (!ec)  // Timer expired
        {
            std::cout << "[TIMER] Keepalive timeout for session: " << session_id << std::endl;
            on_timeout();
        }
        else if (ec == asio::error::operation_aborted)
        {
            std::cout << "[TIMER] Keepalive timer cancelled for session: " << session_id << std::endl;
        }
    });
    
    m_keepalive_timers[session_id] = timer;
    
    std::cout << "[TIMER] Started keepalive timer for session: " << session_id 
              << " (timeout: " << timeout_seconds << "s)" << std::endl;
}

void timer_manager::reset_keepalive_timer(const std::string& session_id)
{
    // PLACEHOLDER: To be fully implemented when keepalive feature is added
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_keepalive_timers.find(session_id);
    if (it != m_keepalive_timers.end() && it->second)
    {
        std::cout << "[TIMER] Reset keepalive timer for session: " << session_id << " (PLACEHOLDER)" << std::endl;
        // Note: Full implementation will need to store callback or use a different approach
    }
}

void timer_manager::cancel_keepalive_timer(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_keepalive_timers.find(session_id);
    if (it != m_keepalive_timers.end())
    {
        if (it->second)
        {
            it->second->cancel();
        }
        m_keepalive_timers.erase(it);
        std::cout << "[TIMER] Cancelled keepalive timer for session: " << session_id << std::endl;
    }
}

// ==================== SESSION CLEANUP ====================

void timer_manager::clear_session_timers(const std::string& session_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Cancel all ACK timers for this session
    auto ack_it = m_ack_timers.find(session_id);
    if (ack_it != m_ack_timers.end())
    {
        for (auto& timer_pair : ack_it->second)
        {
            if (timer_pair.second)
            {
                timer_pair.second->cancel();
            }
        }
        m_ack_timers.erase(ack_it);
    }
    
    // Cancel keepalive timer (inline to avoid deadlock)
    auto ka_it = m_keepalive_timers.find(session_id);
    if (ka_it != m_keepalive_timers.end())
    {
        if (ka_it->second)
        {
            ka_it->second->cancel();
        }
        m_keepalive_timers.erase(ka_it);
        std::cout << "[TIMER] Cancelled keepalive timer for session: " << session_id << std::endl;
    }
    
    std::cout << "[TIMER] Cleared all timers for session: " << session_id << std::endl;
}


