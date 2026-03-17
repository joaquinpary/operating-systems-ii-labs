#include "timer_manager.hpp"

#include <common/logger.h>

#include <cstring>
#include <iostream>
#include <sys/timerfd.h>
#include <unistd.h>

timer_manager::timer_manager(event_loop& loop) : m_loop(loop)
{
    m_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timerfd == -1)
    {
        std::cerr << "[TIMER] timerfd_create fail: " << strerror(errno) << '\n';
        LOG_ERROR_MSG("[TIMER] timerfd_create fail: %s", strerror(errno));
        return;
    }

    m_loop.add_fd(m_timerfd, EPOLLIN, [this](std::uint32_t /*events*/) { on_timerfd_ready(); });

    LOG_INFO_MSG("[TIMER] init timerfd=%d", m_timerfd);
}

timer_manager::~timer_manager()
{
    m_heap.clear();
    m_index.clear();

    if (m_timerfd != -1)
    {
        m_loop.remove_fd(m_timerfd);
        ::close(m_timerfd);
    }

    LOG_INFO_MSG("[TIMER] destroyed");
}

// ==================== INTERNAL HELPERS ====================

void timer_manager::insert_timer(const timer_key& key, int timeout_seconds, std::function<void()> callback)
{
    // Cancel any existing timer with the same key
    cancel_timer(key);

    auto deadline = clock_t::now() + std::chrono::seconds(timeout_seconds);

    timer_entry entry;
    entry.key = key;
    entry.callback = std::move(callback);

    auto it = m_heap.emplace(deadline, std::move(entry));
    m_index[key] = it;

    rearm_timerfd();
}

bool timer_manager::cancel_timer(const timer_key& key)
{
    auto idx_it = m_index.find(key);
    if (idx_it == m_index.end())
    {
        return false;
    }

    bool was_earliest = (idx_it->second == m_heap.begin());

    m_heap.erase(idx_it->second);
    m_index.erase(idx_it);

    // Only re-arm if we removed the earliest timer (otherwise the timerfd is still correct)
    if (was_earliest)
    {
        rearm_timerfd();
    }

    return true;
}

void timer_manager::rearm_timerfd()
{
    struct itimerspec ts
    {
    };

    if (m_heap.empty())
    {
        // Disarm: setting both fields to 0 disarms the timerfd
        timerfd_settime(m_timerfd, 0, &ts, nullptr);
        return;
    }

    auto now = clock_t::now();
    auto earliest = m_heap.begin()->first;
    auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(earliest - now);

    if (delta.count() <= 0)
    {
        // Already expired — fire as soon as possible
        ts.it_value.tv_nsec = 1;
    }
    else
    {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(delta);
        auto nsecs = delta - secs;
        ts.it_value.tv_sec = secs.count();
        ts.it_value.tv_nsec = nsecs.count();
        if (ts.it_value.tv_sec == 0 && ts.it_value.tv_nsec == 0)
        {
            ts.it_value.tv_nsec = 1;
        }
    }

    // One-shot (it_interval stays zero)
    if (timerfd_settime(m_timerfd, 0, &ts, nullptr) == -1)
    {
        std::cerr << "[TIMER] settime fail: " << strerror(errno) << '\n';
        LOG_ERROR_MSG("[TIMER] settime fail: %s", strerror(errno));
    }
}

void timer_manager::on_timerfd_ready()
{
    // Read the timerfd to acknowledge expiry (required to disarm)
    std::uint64_t expirations;
    (void)read(m_timerfd, &expirations, sizeof(expirations));

    auto now = clock_t::now();

    // Pop and fire all expired timers.
    // We collect callbacks first, then invoke them, because a callback
    // might insert or cancel other timers (mutating m_heap / m_index).
    std::vector<std::function<void()>> to_fire;

    while (!m_heap.empty())
    {
        auto it = m_heap.begin();
        if (it->first > now)
        {
            break; // No more expired entries
        }

        // Remove from index
        m_index.erase(it->second.key);

        to_fire.push_back(std::move(it->second.callback));
        m_heap.erase(it);
    }

    // Re-arm BEFORE firing callbacks so that any timers created inside callbacks
    // can further adjust the timerfd.
    rearm_timerfd();

    if (!to_fire.empty())
    {
        LOG_DEBUG_MSG("[TIMER] fired %zu expired timers", to_fire.size());
    }

    for (auto& cb : to_fire)
    {
        cb();
    }
}

// ==================== ACK TIMERS ====================

void timer_manager::start_ack_timer(const std::string& session_id, const std::string& msg_timestamp,
                                    int timeout_seconds, std::function<void()> on_timeout)
{
    timer_key key{"ack", session_id, msg_timestamp};
    insert_timer(key, timeout_seconds, std::move(on_timeout));

    LOG_INFO_MSG("[TIMER] +ACK sess=%s ts=%s timeout=%ds", session_id.c_str(), msg_timestamp.c_str(), timeout_seconds);
}

bool timer_manager::cancel_ack_timer(const std::string& session_id, const std::string& msg_timestamp)
{
    timer_key key{"ack", session_id, msg_timestamp};
    bool found = cancel_timer(key);

    if (found)
    {
        LOG_INFO_MSG("[TIMER] -ACK sess=%s ts=%s", session_id.c_str(), msg_timestamp.c_str());
    }

    return found;
}

// ==================== KEEPALIVE TIMERS ====================

void timer_manager::start_keepalive_timer(const std::string& session_id, int timeout_seconds,
                                          std::function<void()> on_timeout)
{
    // Cancel existing keepalive for this session
    cancel_keepalive_timer(session_id);

    // Store timeout + callback for future resets
    m_keepalive_timeouts[session_id] = timeout_seconds;
    m_keepalive_callbacks[session_id] = on_timeout;

    timer_key key{"keepalive", session_id, ""};
    insert_timer(key, timeout_seconds, on_timeout);

    LOG_INFO_MSG("[TIMER] +KA sess=%s timeout=%ds", session_id.c_str(), timeout_seconds);
}

void timer_manager::reset_keepalive_timer(const std::string& session_id)
{
    auto timeout_it = m_keepalive_timeouts.find(session_id);
    if (timeout_it == m_keepalive_timeouts.end())
    {
        return;
    }

    auto cb_it = m_keepalive_callbacks.find(session_id);
    if (cb_it == m_keepalive_callbacks.end())
    {
        return;
    }

    timer_key key{"keepalive", session_id, ""};
    insert_timer(key, timeout_it->second, cb_it->second);

    LOG_DEBUG_MSG("[TIMER] ~KA sess=%s reset=%ds", session_id.c_str(), timeout_it->second);
}

void timer_manager::cancel_keepalive_timer(const std::string& session_id)
{
    timer_key key{"keepalive", session_id, ""};
    if (cancel_timer(key))
    {
        LOG_INFO_MSG("[TIMER] -KA sess=%s", session_id.c_str());
    }
    m_keepalive_timeouts.erase(session_id);
    m_keepalive_callbacks.erase(session_id);
}

// ==================== SESSION CLEANUP ====================

void timer_manager::clear_session_timers(const std::string& session_id)
{
    // Collect all timer_keys belonging to this session, then erase them.
    // We can't erase while iterating m_index because cancel_timer mutates it.
    std::vector<timer_key> to_cancel;
    for (auto& [key, _] : m_index)
    {
        if (key.session == session_id)
        {
            to_cancel.push_back(key);
        }
    }

    for (auto& key : to_cancel)
    {
        cancel_timer(key);
    }

    m_keepalive_timeouts.erase(session_id);
    m_keepalive_callbacks.erase(session_id);
}
