#ifndef MQTT_CLIENT_HPP
#define MQTT_CLIENT_HPP

#include "config.hpp"
#include "event_loop.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct mosquitto;

/// 15 predefined delivery destinations.
inline constexpr const char* DELIVERY_DESTINATIONS[] = {
    "Mercado Sur",       "Mercado Norte",    "FCEFyN",          "Plaza Espana",   "Patio Olmos",
    "Nueva Cordoba",     "Alto Alberdi",     "Guemes",          "San Vicente",    "Centro",
    "Cerro Campanas",    "Chateau Carreras", "Villa Belgrano",  "Arguello",       "La Calera",
};
inline constexpr std::size_t NUM_DESTINATIONS = sizeof(DELIVERY_DESTINATIONS) / sizeof(DELIVERY_DESTINATIONS[0]);

/// Information about an active courier discovered via tracking.
struct courier_info
{
    std::chrono::steady_clock::time_point last_seen;
    std::vector<std::string> pending_stops; ///< Each entry: "DestinationName TXN_ID"
};

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

    /**
     * Assign a new delivery stop for the given transaction to an active courier.
     * Picks a random destination, adds it to the courier's pending stops,
     * and publishes the full route to routes/{employee_id}.
     * @return 0 on success, -1 if no active couriers.
     */
    int assign_route(int transaction_id);

    /** Get the courier registry (read-only). */
    const std::unordered_map<std::string, courier_info>& couriers() const { return m_couriers; }

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

    /// Topic-specific handlers.
    void handle_tracking(const std::string& employee_id, const std::string& payload);

    struct mosquitto* m_mosq = nullptr;
    event_loop& m_loop;
    std::string m_host;
    std::uint16_t m_port;
    int m_registered_fd = -1; ///< Currently registered fd in event_loop (-1 = none).
    int m_misc_timer_fd = -1; ///< timerfd for periodic loop_misc calls.

    /// Active couriers discovered via tracking/+ messages.
    std::unordered_map<std::string, courier_info> m_couriers;

    /// Round-robin index for courier assignment.
    std::size_t m_rr_index = 0;
};

#endif // MQTT_CLIENT_HPP
