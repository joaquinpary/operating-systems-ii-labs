#ifndef TIMER_MANAGER_HPP
#define TIMER_MANAGER_HPP

#include "event_loop.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <string>

/**
 * Manages ACK and keepalive timers using a SINGLE Linux timerfd + in-memory min-heap.
 *
 * Instead of creating one timerfd per timer (which exhausts file descriptors at scale),
 * all timers are stored in a std::multimap (ordered by expiry time) and a single timerfd
 * is armed to fire at the earliest deadline. When it fires, all expired entries are
 * processed and the timerfd is re-armed to the next deadline.
 *
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
    using clock_t = std::chrono::steady_clock;
    using time_point_t = clock_t::time_point;

    /// Unique key for a timer entry (type + session_id + sub_key).
    /// type: "ack" or "keepalive"
    /// sub_key: msg_timestamp for ACK timers, empty for keepalive.
    struct timer_key
    {
        std::string type; // "ack" or "keepalive"
        std::string session;
        std::string sub_key; // msg_timestamp for ACK, "" for keepalive

        bool operator==(const timer_key& o) const
        {
            return type == o.type && session == o.session && sub_key == o.sub_key;
        }
        bool operator<(const timer_key& o) const
        {
            if (type != o.type)
                return type < o.type;
            if (session != o.session)
                return session < o.session;
            return sub_key < o.sub_key;
        }
    };

    struct timer_entry
    {
        timer_key key;
        std::function<void()> callback;
    };

    /// The min-heap: ordered by expiry time → timer_entry.
    /// std::multimap keeps entries sorted by key (time_point_t), earliest first.
    std::multimap<time_point_t, timer_entry> m_heap;

    /// Reverse index: timer_key → iterator into m_heap (for O(log n) cancel).
    std::map<timer_key, std::multimap<time_point_t, timer_entry>::iterator> m_index;

    /// Keepalive timeout per session (to re-arm on reset).
    std::map<std::string, int> m_keepalive_timeouts;

    /// Keepalive callback per session (to re-arm on reset).
    std::map<std::string, std::function<void()>> m_keepalive_callbacks;

    event_loop& m_loop;
    int m_timerfd = -1; ///< The single timerfd shared by all timers.

    /// Insert a timer into the heap + index and re-arm if needed.
    void insert_timer(const timer_key& key, int timeout_seconds, std::function<void()> callback);

    /// Cancel a timer by key, returns true if found.
    bool cancel_timer(const timer_key& key);

    /// Re-arm the single timerfd to fire at the earliest deadline (or disarm if empty).
    void rearm_timerfd();

    /// Called by epoll when the timerfd fires.
    void on_timerfd_ready();
};

#endif // TIMER_MANAGER_HPP
