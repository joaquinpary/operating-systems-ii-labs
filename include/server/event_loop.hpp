#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP

#include <cstdint>
#include <functional>
#include <sys/epoll.h>
#include <unordered_map>

/// Maximum number of events returned per epoll_wait call.
inline constexpr int MAX_EPOLL_EVENTS = 256;

/**
 * Single-threaded event loop based on Linux epoll.
 * Only the reactor thread should call run() and the fd callbacks.
 */
class event_loop
{
  public:
    using fd_callback_t = std::function<void(std::uint32_t events)>;

    event_loop();
    ~event_loop();

    // Non-copyable, non-movable
    event_loop(const event_loop&) = delete;
    event_loop& operator=(const event_loop&) = delete;

    /**
     * Register a file descriptor with the event loop.
     * @param fd   File descriptor to monitor.
     * @param events  epoll events (EPOLLIN, EPOLLOUT, EPOLLET, etc.)
     * @param cb   Callback invoked when events fire on this fd.
     */
    void add_fd(int fd, std::uint32_t events, fd_callback_t cb);

    /**
     * Modify the monitored events for a registered fd.
     */
    void modify_fd(int fd, std::uint32_t events);

    /**
     * Remove a file descriptor from the event loop.
     * Does NOT close the fd.
     */
    void remove_fd(int fd);

    /**
     * Run the event loop (blocks until stop() is called).
     */
    void run();

    /**
     * Signal the event loop to stop after the current iteration.
     */
    void stop();

    /**
     * @return true if the loop is running.
     */
    bool is_running() const;

  private:
    int m_epoll_fd;
    int m_wakeup_fd; ///< Internal eventfd used by stop() to wake up epoll_wait.
    bool m_running;
    std::unordered_map<int, fd_callback_t> m_callbacks;
};

#endif // EVENT_LOOP_HPP
