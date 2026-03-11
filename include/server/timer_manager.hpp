#ifndef TIMER_MANAGER_HPP
#define TIMER_MANAGER_HPP

#include "event_loop.hpp"

#include <functional>
#include <map>
#include <string>

/**
 * Manages ACK and keepalive timers using Linux timerfd.
 * All methods MUST be called from the reactor thread only — no mutex needed.
 */
class timer_manager
{
  public:
    explicit timer_manager(event_loop& loop);
    ~timer_manager();

    // ==================== ACK TIMERS ====================

    void start_ack_timer(const std::string& session_id, const std::string& msg_timestamp, int timeout_seconds,
                         std::function<void()> on_timeout);

    bool cancel_ack_timer(const std::string& session_id, const std::string& msg_timestamp);

    // ==================== KEEPALIVE TIMERS ====================

    void start_keepalive_timer(const std::string& session_id, int timeout_seconds, std::function<void()> on_timeout);
    void reset_keepalive_timer(const std::string& session_id);
    void cancel_keepalive_timer(const std::string& session_id);

    // ==================== SESSION CLEANUP ====================

    void clear_session_timers(const std::string& session_id);

  private:
    void close_timerfd(int fd);

    event_loop& m_loop;

    // ACK timers: session_id → (msg_timestamp → timerfd)
    std::map<std::string, std::map<std::string, int>> m_ack_timers;

    // Keepalive timers: session_id → timerfd
    std::map<std::string, int> m_keepalive_timers;

    // Keepalive timeout per session (to re-arm on reset)
    std::map<std::string, int> m_keepalive_timeouts;
};

#endif // TIMER_MANAGER_HPP
