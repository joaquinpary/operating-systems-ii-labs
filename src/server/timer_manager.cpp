#include "timer_manager.hpp"

#include <cstring>
#include <iostream>
#include <sys/timerfd.h>
#include <unistd.h>

timer_manager::timer_manager(event_loop& loop) : m_loop(loop)
{
    std::cout << "[TIMER_MANAGER] Initialized" << std::endl;
}

timer_manager::~timer_manager()
{
    // Cancel all ACK timers
    for (auto& [session_id, timer_map] : m_ack_timers)
    {
        for (auto& [msg_ts, tfd] : timer_map)
        {
            close_timerfd(tfd);
        }
    }
    m_ack_timers.clear();

    // Cancel all keepalive timers
    for (auto& [session_id, tfd] : m_keepalive_timers)
    {
        close_timerfd(tfd);
    }
    m_keepalive_timers.clear();

    std::cout << "[TIMER_MANAGER] Destroyed" << std::endl;
}

// ==================== ACK TIMERS ====================

void timer_manager::start_ack_timer(const std::string& session_id, const std::string& msg_timestamp,
                                    int timeout_seconds, std::function<void()> on_timeout)
{
    // Cancel existing timer for this session/timestamp if any
    cancel_ack_timer(session_id, msg_timestamp);

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd == -1)
    {
        std::cerr << "[TIMER] timerfd_create failed: " << strerror(errno) << std::endl;
        return;
    }

    struct itimerspec ts
    {
    };
    ts.it_value.tv_sec = timeout_seconds;
    // timerfd disarms when both tv_sec and tv_nsec are 0; use 1ns for immediate fire
    ts.it_value.tv_nsec = (timeout_seconds == 0) ? 1 : 0;
    ts.it_interval.tv_sec = 0; // One-shot
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(tfd, 0, &ts, nullptr) == -1)
    {
        std::cerr << "[TIMER] timerfd_settime failed: " << strerror(errno) << std::endl;
        ::close(tfd);
        return;
    }

    // Register with event loop (level-triggered for timerfd)
    m_loop.add_fd(tfd, EPOLLIN, [this, tfd, session_id, msg_timestamp, on_timeout](std::uint32_t /*events*/) {
        // Read the timerfd to acknowledge expiry
        std::uint64_t expirations;
        ssize_t n = read(tfd, &expirations, sizeof(expirations));
        (void)n; // We don't need the count

        std::cout << "[TIMER] ACK timeout for session: " << session_id << ", message: " << msg_timestamp << std::endl;

        // Clean up this timerfd from our map and epoll before calling callback
        close_timerfd(tfd);

        auto session_it = m_ack_timers.find(session_id);
        if (session_it != m_ack_timers.end())
        {
            session_it->second.erase(msg_timestamp);
            if (session_it->second.empty())
            {
                m_ack_timers.erase(session_it);
            }
        }

        // Execute timeout callback
        on_timeout();
    });

    m_ack_timers[session_id][msg_timestamp] = tfd;

    std::cout << "[TIMER] Started ACK timer for session: " << session_id << ", message: " << msg_timestamp
              << " (timeout: " << timeout_seconds << "s)" << std::endl;
}

bool timer_manager::cancel_ack_timer(const std::string& session_id, const std::string& msg_timestamp)
{
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

    close_timerfd(timer_it->second);
    session_it->second.erase(timer_it);

    if (session_it->second.empty())
    {
        m_ack_timers.erase(session_it);
    }

    std::cout << "[TIMER] Cancelled ACK timer for session: " << session_id << ", message: " << msg_timestamp
              << std::endl;
    return true;
}

// ==================== KEEPALIVE TIMERS ====================

void timer_manager::start_keepalive_timer(const std::string& session_id, int timeout_seconds,
                                          std::function<void()> on_timeout)
{
    // Cancel existing
    cancel_keepalive_timer(session_id);

    // Store timeout for future resets
    m_keepalive_timeouts[session_id] = timeout_seconds;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd == -1)
    {
        std::cerr << "[TIMER] timerfd_create failed: " << strerror(errno) << std::endl;
        return;
    }

    struct itimerspec ts
    {
    };
    ts.it_value.tv_sec = timeout_seconds;
    // timerfd disarms when both tv_sec and tv_nsec are 0; use 1ns for immediate fire
    ts.it_value.tv_nsec = (timeout_seconds == 0) ? 1 : 0;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(tfd, 0, &ts, nullptr) == -1)
    {
        std::cerr << "[TIMER] timerfd_settime failed: " << strerror(errno) << std::endl;
        ::close(tfd);
        return;
    }

    m_loop.add_fd(tfd, EPOLLIN, [this, tfd, session_id, on_timeout](std::uint32_t /*events*/) {
        std::uint64_t expirations;
        ssize_t n = read(tfd, &expirations, sizeof(expirations));
        (void)n;

        std::cout << "[TIMER] Keepalive timeout for session: " << session_id << std::endl;

        close_timerfd(tfd);
        m_keepalive_timers.erase(session_id);

        on_timeout();
    });

    m_keepalive_timers[session_id] = tfd;

    std::cout << "[TIMER] Started keepalive timer for session: " << session_id << " (timeout: " << timeout_seconds
              << "s)" << std::endl;
}

void timer_manager::reset_keepalive_timer(const std::string& session_id)
{
    auto it = m_keepalive_timers.find(session_id);
    if (it == m_keepalive_timers.end())
    {
        return;
    }

    auto timeout_it = m_keepalive_timeouts.find(session_id);
    if (timeout_it == m_keepalive_timeouts.end())
    {
        return;
    }

    int timeout_seconds = timeout_it->second;
    int tfd = it->second;

    struct itimerspec ts
    {
    };
    ts.it_value.tv_sec = timeout_seconds;
    ts.it_value.tv_nsec = (timeout_seconds == 0) ? 1 : 0;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(tfd, 0, &ts, nullptr) == -1)
    {
        std::cerr << "[TIMER] Failed to reset keepalive timerfd for session: " << session_id << ": " << strerror(errno)
                  << std::endl;
        return;
    }

    std::cout << "[TIMER] Reset keepalive timer for session: " << session_id << " (" << timeout_seconds << "s)"
              << std::endl;
}

void timer_manager::cancel_keepalive_timer(const std::string& session_id)
{
    auto it = m_keepalive_timers.find(session_id);
    if (it != m_keepalive_timers.end())
    {
        close_timerfd(it->second);
        m_keepalive_timers.erase(it);
        std::cout << "[TIMER] Cancelled keepalive timer for session: " << session_id << std::endl;
    }
    m_keepalive_timeouts.erase(session_id);
}

// ==================== SESSION CLEANUP ====================

void timer_manager::clear_session_timers(const std::string& session_id)
{
    auto ack_it = m_ack_timers.find(session_id);
    if (ack_it != m_ack_timers.end())
    {
        for (auto& [msg_ts, tfd] : ack_it->second)
        {
            close_timerfd(tfd);
        }
        m_ack_timers.erase(ack_it);
    }

    cancel_keepalive_timer(session_id);
}

// ==================== Helper ====================

void timer_manager::close_timerfd(int fd)
{
    m_loop.remove_fd(fd);
    ::close(fd);
}
