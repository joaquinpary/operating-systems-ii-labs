#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include "config.hpp"
#include <cstdint>
#include <functional>
#include <string>

struct mosquitto;

/**
 * MQTT client for the reactor event loop.
 * Wraps libmosquitto and exposes the raw socket fd for epoll integration.
 */
class mqtt_client
{
  public:
    explicit mqtt_client(const config::server_config& cfg);
    ~mqtt_client();

    mqtt_client(const mqtt_client&) = delete;
    mqtt_client& operator=(const mqtt_client&) = delete;

    /** Connect to the MQTT broker (non-blocking). */
    int connect();

    /** Disconnect from the MQTT broker. */
    void disconnect();

    /** Publish a message to the given topic. */
    int publish(const std::string& topic, const std::string& payload, int qos = 1);

    /** Return the underlying socket fd for epoll registration. Returns -1 if not connected. */
    int get_socket_fd() const;

    /** Drive the read side of the MQTT socket (call when epoll signals EPOLLIN). */
    int on_readable();

    /** Drive the write side of the MQTT socket (call when epoll signals EPOLLOUT). */
    int on_writable();

    /** Periodic housekeeping (keepalive, reconnect). Call from a timerfd. */
    int loop_misc();

    /** True if the underlying library wants to write (EPOLLOUT should be armed). */
    bool want_write() const;

  private:
    struct mosquitto* m_mosq = nullptr;
    std::string m_host;
    std::uint16_t m_port;
};

#endif // MQTT_CLIENT_HPP
