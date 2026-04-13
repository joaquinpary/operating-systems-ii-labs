#include "mqtt_client.hpp"

#include "database.hpp"
#include <algorithm>
#include <cJSON.h>
#include <common/logger.h>
#include <cstring>
#include <iostream>
#include <mosquitto.h>
#include <random>
#include <sys/timerfd.h>
#include <unistd.h>

/// One-time libmosquitto initialisation guard.
static struct mosquitto_init_guard
{
    mosquitto_init_guard() { mosquitto_lib_init(); }
    ~mosquitto_init_guard() { mosquitto_lib_cleanup(); }
} s_mosq_guard;

// ---------------------------------------------------------------------------
// Static callback trampolines
// ---------------------------------------------------------------------------

void mqtt_client::on_connect_cb(struct mosquitto* /*mosq*/, void* userdata, int rc)
{
    static_cast<mqtt_client*>(userdata)->handle_connect(rc);
}

void mqtt_client::on_message_cb(struct mosquitto* /*mosq*/, void* userdata, const struct mosquitto_message* msg)
{
    auto* self = static_cast<mqtt_client*>(userdata);
    std::string topic(msg->topic ? msg->topic : "");
    std::string payload(static_cast<const char*>(msg->payload), static_cast<std::size_t>(msg->payloadlen));
    self->handle_message(topic, payload);
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

mqtt_client::mqtt_client(const config::server_config& cfg, event_loop& loop, std::shared_ptr<connection_pool> db_pool)
    : m_loop(loop), m_host(cfg.mqtt_broker_host), m_port(cfg.mqtt_broker_port), m_db_pool(std::move(db_pool))
{
    m_mosq = mosquitto_new("dhl_server", true, this);
    if (!m_mosq)
    {
        throw std::runtime_error("[MQTT] mosquitto_new failed");
    }
    mosquitto_connect_callback_set(m_mosq, on_connect_cb);
    mosquitto_message_callback_set(m_mosq, on_message_cb);
}

mqtt_client::~mqtt_client()
{
    disconnect();
    if (m_mosq)
    {
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
    if (m_misc_timer_fd != -1)
    {
        m_loop.remove_fd(m_misc_timer_fd);
        ::close(m_misc_timer_fd);
        m_misc_timer_fd = -1;
    }
}

// ---------------------------------------------------------------------------
// Connect + epoll wiring
// ---------------------------------------------------------------------------

int mqtt_client::connect()
{
    int rc = mosquitto_connect(m_mosq, m_host.c_str(), m_port, 60);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        std::cerr << "[MQTT] connect failed: " << mosquitto_strerror(rc) << '\n';
        LOG_ERROR_MSG("[MQTT] connect failed: %s", mosquitto_strerror(rc));
        return rc;
    }

    std::cout << "[MQTT] connected to " << m_host << ":" << m_port << '\n';
    LOG_INFO_MSG("[MQTT] connected to %s:%d", m_host.c_str(), m_port);

    register_socket();

    // Create a timerfd for periodic mosquitto_loop_misc() (keepalive / ping).
    m_misc_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_misc_timer_fd == -1)
    {
        LOG_ERROR_MSG("[MQTT] timerfd_create failed: %s", strerror(errno));
        return -1;
    }

    struct itimerspec ts = {};
    ts.it_interval.tv_sec = 15; // every 15 s
    ts.it_value.tv_sec = 15;
    timerfd_settime(m_misc_timer_fd, 0, &ts, nullptr);

    m_loop.add_fd(m_misc_timer_fd, EPOLLIN, [this](std::uint32_t) {
        std::uint64_t exp;
        (void)::read(m_misc_timer_fd, &exp, sizeof(exp));
        loop_misc();
    });

    return 0;
}

void mqtt_client::disconnect()
{
    unregister_socket();
    if (m_mosq)
    {
        mosquitto_disconnect(m_mosq);
    }
}

// ---------------------------------------------------------------------------
// Publish
// ---------------------------------------------------------------------------

int mqtt_client::publish(const std::string& topic, const std::string& payload, int qos)
{
    int rc =
        mosquitto_publish(m_mosq, nullptr, topic.c_str(), static_cast<int>(payload.size()), payload.c_str(), qos, false);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        std::cerr << "[MQTT] publish to " << topic << " failed: " << mosquitto_strerror(rc) << '\n';
        LOG_ERROR_MSG("[MQTT] publish to %s failed: %s", topic.c_str(), mosquitto_strerror(rc));
    }
    else
    {
        LOG_INFO_MSG("[MQTT] published to %s (%zu bytes)", topic.c_str(), payload.size());
    }

    // After publish there may be data to write — update epoll interest.
    if (mosquitto_want_write(m_mosq))
    {
        int fd = mosquitto_socket(m_mosq);
        if (fd >= 0 && fd == m_registered_fd)
        {
            m_loop.modify_fd(fd, EPOLLIN | EPOLLOUT);
        }
    }

    return rc;
}

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------

int mqtt_client::loop_misc()
{
    return mosquitto_loop_misc(m_mosq);
}

// ---------------------------------------------------------------------------
// epoll socket management
// ---------------------------------------------------------------------------

void mqtt_client::register_socket()
{
    int fd = mosquitto_socket(m_mosq);
    if (fd < 0)
        return;

    if (fd == m_registered_fd)
        return; // already registered

    unregister_socket(); // remove stale fd if any

    std::uint32_t events = EPOLLIN;
    if (mosquitto_want_write(m_mosq))
        events |= EPOLLOUT;

    m_loop.add_fd(fd, events, [this](std::uint32_t ev) {
        if (ev & EPOLLIN)
        {
            mosquitto_loop_read(m_mosq, 1);
        }
        if (ev & EPOLLOUT)
        {
            mosquitto_loop_write(m_mosq, 1);
        }

        // Re-arm EPOLLOUT only when the library has pending writes.
        int sock = mosquitto_socket(m_mosq);
        if (sock >= 0 && sock == m_registered_fd)
        {
            std::uint32_t next = EPOLLIN;
            if (mosquitto_want_write(m_mosq))
                next |= EPOLLOUT;
            m_loop.modify_fd(sock, next);
        }
    });

    m_registered_fd = fd;
}

void mqtt_client::unregister_socket()
{
    if (m_registered_fd >= 0)
    {
        m_loop.remove_fd(m_registered_fd);
        m_registered_fd = -1;
    }
}

// ---------------------------------------------------------------------------
// MQTT event handlers
// ---------------------------------------------------------------------------

void mqtt_client::handle_connect(int rc)
{
    if (rc != 0)
    {
        std::cerr << "[MQTT] on_connect error rc=" << rc << '\n';
        LOG_ERROR_MSG("[MQTT] on_connect error rc=%d", rc);
        return;
    }

    LOG_INFO_MSG("[MQTT] connected — subscribing to topics");
    std::cout << "[MQTT] connected — subscribing to topics\n";

    mosquitto_subscribe(m_mosq, nullptr, "tracking/+", 0);
    mosquitto_subscribe(m_mosq, nullptr, "delivered/+", 1);
    mosquitto_subscribe(m_mosq, nullptr, "alerts/sos/+", 1);
}

void mqtt_client::handle_message(const std::string& topic, const std::string& payload)
{
    LOG_INFO_MSG("[MQTT] msg topic=%s payload=%s", topic.c_str(), payload.c_str());

    // Route to topic-specific handlers based on prefix.
    if (topic.rfind("tracking/", 0) == 0)
    {
        std::string employee_id = topic.substr(9); // len("tracking/") == 9
        handle_tracking(employee_id, payload);
    }
    else if (topic.rfind("delivered/", 0) == 0)
    {
        std::string employee_id = topic.substr(10); // len("delivered/") == 10
        handle_delivered(employee_id, payload);
    }
    else if (topic.rfind("alerts/sos/", 0) == 0)
    {
        std::string employee_id = topic.substr(11); // len("alerts/sos/") == 11
        handle_sos(employee_id, payload);
    }
}

// ---------------------------------------------------------------------------
// Topic handlers
// ---------------------------------------------------------------------------

void mqtt_client::handle_tracking(const std::string& employee_id, const std::string& /*payload*/)
{
    auto& courier = m_couriers[employee_id];
    courier.last_seen = std::chrono::steady_clock::now();
    LOG_DEBUG_MSG("[MQTT] tracking update courier=%s", employee_id.c_str());
}

// ---------------------------------------------------------------------------
// Route assignment
// ---------------------------------------------------------------------------

int mqtt_client::assign_route(int transaction_id)
{
    if (m_couriers.empty())
    {
        std::cerr << "[MQTT] no active couriers, cannot assign route for txn=" << transaction_id << '\n';
        LOG_ERROR_MSG("[MQTT] no active couriers for txn=%d", transaction_id);
        return -1;
    }

    // Round-robin: advance index and wrap around.
    auto it = m_couriers.begin();
    std::advance(it, m_rr_index % m_couriers.size());
    ++m_rr_index;

    auto& [employee_id, courier] = *it;

    // Pick a random destination from the 15 predefined stops.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, NUM_DESTINATIONS - 1);
    const char* dest = DELIVERY_DESTINATIONS[dist(rng)];

    // Build stop string: "DestinationName TXN_ID"
    std::string stop = std::string(dest) + " " + std::to_string(transaction_id);
    courier.pending_stops.push_back(stop);

    LOG_INFO_MSG("[MQTT] assigned stop \"%s\" to courier=%s", stop.c_str(), employee_id.c_str());
    std::cout << "[MQTT] assigned stop \"" << stop << "\" to courier=" << employee_id << '\n';

    // Publish only the new stop — firmware appends to its internal route list.
    std::string json = "[\"" + stop + "\"]";
    std::string topic = "routes/" + employee_id;
    publish(topic, json, 1);

    return 0;
}

// ---------------------------------------------------------------------------
// Delivered handler
// ---------------------------------------------------------------------------

void mqtt_client::handle_delivered(const std::string& employee_id, const std::string& payload)
{
    // Parse: {"stop": "Mercado Norte 54", "status": "done"}
    cJSON* json = cJSON_Parse(payload.c_str());
    if (!json)
    {
        LOG_ERROR_MSG("[MQTT] delivered: invalid JSON from courier=%s", employee_id.c_str());
        return;
    }

    cJSON* stop_field = cJSON_GetObjectItemCaseSensitive(json, "stop");
    if (!stop_field || !cJSON_IsString(stop_field))
    {
        LOG_ERROR_MSG("[MQTT] delivered: missing 'stop' field from courier=%s", employee_id.c_str());
        cJSON_Delete(json);
        return;
    }

    std::string stop_str(stop_field->valuestring);
    cJSON_Delete(json);

    // Extract transaction_id from the trailing number: "Mercado Norte 54" → 54
    int transaction_id = -1;
    auto last_space = stop_str.rfind(' ');
    if (last_space != std::string::npos)
    {
        try
        {
            transaction_id = std::stoi(stop_str.substr(last_space + 1));
        }
        catch (...)
        {
            LOG_ERROR_MSG("[MQTT] delivered: cannot parse txn_id from stop=\"%s\"", stop_str.c_str());
            return;
        }
    }

    if (transaction_id < 0)
    {
        LOG_ERROR_MSG("[MQTT] delivered: invalid txn_id from stop=\"%s\"", stop_str.c_str());
        return;
    }

    // Mark transaction as COMPLETED in DB.
    try
    {
        auto guard = m_db_pool->acquire();
        // Use current UTC time as reception timestamp.
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        char ts_buf[32];
        std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&tt));

        complete_transaction(guard.get(), transaction_id, std::string(ts_buf));

        LOG_INFO_MSG("[MQTT] delivered: txn=%d completed, courier=%s stop=\"%s\"", transaction_id,
                     employee_id.c_str(), stop_str.c_str());
        std::cout << "[MQTT] delivered: txn=" << transaction_id << " completed, courier=" << employee_id << '\n';
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR_MSG("[MQTT] delivered: DB error txn=%d: %s", transaction_id, ex.what());
        std::cerr << "[MQTT] delivered: DB error txn=" << transaction_id << ": " << ex.what() << '\n';
    }

    // Remove the stop from the courier's pending list.
    auto cit = m_couriers.find(employee_id);
    if (cit != m_couriers.end())
    {
        auto& stops = cit->second.pending_stops;
        stops.erase(std::remove(stops.begin(), stops.end(), stop_str), stops.end());
    }
}

// ---------------------------------------------------------------------------
// SOS handler
// ---------------------------------------------------------------------------

void mqtt_client::handle_sos(const std::string& employee_id, const std::string& payload)
{
    // Parse: {"time": "2025-06-01T14:05:00Z", "coordinates": {"lat": -31.4, "lon": -64.2}}
    cJSON* json = cJSON_Parse(payload.c_str());

    std::string time_str = "unknown";
    double lat = 0.0, lon = 0.0;

    if (json)
    {
        cJSON* time_field = cJSON_GetObjectItemCaseSensitive(json, "time");
        if (time_field && cJSON_IsString(time_field))
            time_str = time_field->valuestring;

        cJSON* coords = cJSON_GetObjectItemCaseSensitive(json, "coordinates");
        if (coords)
        {
            cJSON* lat_field = cJSON_GetObjectItemCaseSensitive(coords, "lat");
            cJSON* lon_field = cJSON_GetObjectItemCaseSensitive(coords, "lon");
            if (lat_field && cJSON_IsNumber(lat_field))
                lat = lat_field->valuedouble;
            if (lon_field && cJSON_IsNumber(lon_field))
                lon = lon_field->valuedouble;
        }
        cJSON_Delete(json);
    }

    // CRITICAL alarm — stderr + logger.
    std::cerr << "\n!! SOS ALERT !! courier=" << employee_id << " time=" << time_str << " lat=" << lat
              << " lon=" << lon << '\n';
    LOG_ERROR_MSG("!! SOS ALERT !! courier=%s time=%s lat=%.6f lon=%.6f", employee_id.c_str(), time_str.c_str(), lat,
                  lon);
}
