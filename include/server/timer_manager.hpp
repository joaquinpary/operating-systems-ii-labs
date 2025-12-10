#ifndef TIMER_MANAGER_HPP
#define TIMER_MANAGER_HPP

#include <asio.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class timer_manager
{
  public:
    explicit timer_manager(asio::io_context& io_context);
    ~timer_manager();

    // ==================== ACK TIMERS ====================
    
    /**
     * Start a timer to wait for ACK for a specific message
     * @param session_id Session identifier
     * @param msg_timestamp Timestamp of the message waiting for ACK
     * @param timeout_seconds Timeout duration in seconds
     * @param on_timeout Callback to execute when timer expires (before retry)
     */
    void start_ack_timer(const std::string& session_id, 
                         const std::string& msg_timestamp,
                         int timeout_seconds,
                         std::function<void()> on_timeout);
    
    /**
     * Cancel ACK timer (called when ACK is received)
     * @param session_id Session identifier
     * @param msg_timestamp Timestamp of the message
     * @return true if timer was found and cancelled, false otherwise
     */
    bool cancel_ack_timer(const std::string& session_id, const std::string& msg_timestamp);
    
    // ==================== KEEPALIVE TIMERS (PLACEHOLDER) ====================
    // Note: Keepalive functionality will be implemented in a future branch
    
    /**
     * Start keepalive timer for a session (PLACEHOLDER)
     * @param session_id Session identifier
     * @param timeout_seconds Timeout duration in seconds
     * @param on_timeout Callback to execute when timer expires (disconnect client)
     */
    void start_keepalive_timer(const std::string& session_id, int timeout_seconds, std::function<void()> on_timeout);
    
    /**
     * Reset keepalive timer (PLACEHOLDER)
     * @param session_id Session identifier
     */
    void reset_keepalive_timer(const std::string& session_id);
    
    /**
     * Cancel keepalive timer for a session (PLACEHOLDER)
     * @param session_id Session identifier
     */
    void cancel_keepalive_timer(const std::string& session_id);
    
    // ==================== SESSION CLEANUP ====================
    
    /**
     * Clear all timers associated with a session
     * @param session_id Session identifier
     */
    void clear_session_timers(const std::string& session_id);

  private:
    asio::io_context& m_io_context;
    
    // ACK timers: session_id -> (msg_timestamp -> timer)
    // its a map of maps, the inner map is a map of message timestamps (timestamps are unique and are used for tracking)
    // to timers for each message timestamp
    // the outer map is a map of session ids to the inner map
    // so the timer manager can have multiple sessions with multiple messages waiting for ACK
    std::map<std::string, std::map<std::string, std::shared_ptr<asio::steady_timer>>> m_ack_timers;
    
    // Keepalive timers: session_id -> timer
    // its a map of session ids to timers for each session
    // so the timer manager can have multiple sessions with a keepalive timer
    std::map<std::string, std::shared_ptr<asio::steady_timer>> m_keepalive_timers;
    
    // mutex to protect the maps from concurrent access
    mutable std::mutex m_mutex;
    
};

#endif // TIMER_MANAGER_HPP

