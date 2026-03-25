#include "event_loop.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>

event_loop::event_loop() : m_running(false)
{
    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd == -1)
    {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
    }

    m_wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeup_fd == -1)
    {
        ::close(m_epoll_fd);
        throw std::runtime_error(std::string("eventfd (wakeup) failed: ") + strerror(errno));
    }

    struct epoll_event ev
    {
    };
    ev.events = EPOLLIN;
    ev.data.fd = m_wakeup_fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_wakeup_fd, &ev) == -1)
    {
        ::close(m_wakeup_fd);
        ::close(m_epoll_fd);
        throw std::runtime_error(std::string("epoll_ctl wakeup fd failed: ") + strerror(errno));
    }
}

event_loop::~event_loop()
{
    if (m_wakeup_fd != -1)
    {
        ::close(m_wakeup_fd);
    }
    if (m_epoll_fd != -1)
    {
        ::close(m_epoll_fd);
    }
}

void event_loop::add_fd(int fd, std::uint32_t events, fd_callback_t cb)
{
    struct epoll_event ev
    {
    };
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
    {
        throw std::runtime_error(std::string("epoll_ctl ADD failed for fd ") + std::to_string(fd) + ": " +
                                 strerror(errno));
    }
    m_callbacks[fd] = std::move(cb);
}

void event_loop::modify_fd(int fd, std::uint32_t events)
{
    struct epoll_event ev
    {
    };
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        throw std::runtime_error(std::string("epoll_ctl MOD failed for fd ") + std::to_string(fd) + ": " +
                                 strerror(errno));
    }
}

void event_loop::remove_fd(int fd)
{
    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    m_callbacks.erase(fd);
}

void event_loop::run()
{
    m_running = true;
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (m_running)
    {
        int n = epoll_wait(m_epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (n == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::cerr << "[EVENT_LOOP] epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == m_wakeup_fd)
            {
                std::uint64_t val;
                (void)read(m_wakeup_fd, &val, sizeof(val));
                continue;
            }

            auto it = m_callbacks.find(fd);
            if (it != m_callbacks.end())
            {
                auto cb = it->second;
                cb(events[i].events);
            }
        }
    }
}

void event_loop::stop()
{
    m_running = false;
    std::uint64_t val = 1;
    (void)write(m_wakeup_fd, &val, sizeof(val));
}

bool event_loop::is_running() const
{
    return m_running;
}
