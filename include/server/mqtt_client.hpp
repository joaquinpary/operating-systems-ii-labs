#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include "config.hpp"
#include "event_loop.hpp"
#include <cstdint>
#include <functional>
#include <string>

struct mosquitto;

/**
 * MQTT client for the reactor event loop.
 * Wraps libmosquitto and integrates with epoll via event_loop.
 */
class mqtt_client
{
  public:
    mqtt_client(const config::server_config& cfg, event_loop& loop);
    ~mqtt_client();

    mqtt_client(const mqtt_client&) = delete;
    mqtt_client& operator=(const mqtt_client&) = delete;

    /** Connect to the MQTT broker and register fd with the event loop. */
    int connect();

    /** Disconnect from the MQTT broker. */
    void disconnect();

    /** Publish a message to the given topic. */
    int publish(const std::string& topic, const std::string& payload, int qos = 1);

    /** Periodic housekeeping (keepalive, reconnect). Call from a timerfd. */
    int loop_misc();

  private:
    /// Register/update the MQTT socket fd in the event loop.
    void register_socket();

    /// Remove the MQTT socket fd from the event loop.
    void unregister_socket();

    /// libmosquitto callbacks (static trampolines → instance methods).
    static void on_connect_cb(struct mosquitto* mosq, void* userdata, int rc);
    static void on_message_cb(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg);

    void handle_connect(int rc);
    void handle_message(const std::string& topic, const std::string& payload);

    struct mosquitto* m_mosq = nullptr;
    event_loop& m_loop;
    std::string m_host;
    std::uint16_t m_port;
    int m_registered_fd = -1; ///< Currently registered fd in event_loop (-1 = none).
    int m_misc_timer_fd = -1; ///< timerfd for periodic loop_misc calls.
};

#endif // MQTT_CLIENT_HPP
