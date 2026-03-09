#include "event_loop.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

event_loop::event_loop() : m_running(false)
{
    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd == -1)
    {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
    }
}

event_loop::~event_loop()
{
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
                continue; // Interrupted by signal, retry
            }
            std::cerr << "[EVENT_LOOP] epoll_wait error: " << strerror(errno) << std::endl;
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;
            auto it = m_callbacks.find(fd);
            if (it != m_callbacks.end())
            {
                it->second(events[i].events);
            }
        }
    }
}

void event_loop::stop()
{
    m_running = false;
}

bool event_loop::is_running() const
{
    return m_running;
}
