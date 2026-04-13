#include "mqtt_client.hpp"

#include <common/logger.h>
#include <iostream>
#include <mosquitto.h>

/// One-time libmosquitto initialisation guard.
static struct mosquitto_init_guard
{
    mosquitto_init_guard() { mosquitto_lib_init(); }
    ~mosquitto_init_guard() { mosquitto_lib_cleanup(); }
} s_mosq_guard;

mqtt_client::mqtt_client(const config::server_config& cfg) : m_host(cfg.mqtt_broker_host), m_port(cfg.mqtt_broker_port)
{
    m_mosq = mosquitto_new("dhl_server", true, this);
    if (!m_mosq)
    {
        throw std::runtime_error("[MQTT] mosquitto_new failed");
    }
}

mqtt_client::~mqtt_client()
{
    disconnect();
    if (m_mosq)
    {
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
}

int mqtt_client::connect()
{
    int rc = mosquitto_connect(m_mosq, m_host.c_str(), m_port, 60);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        std::cerr << "[MQTT] connect failed: " << mosquitto_strerror(rc) << '\n';
        LOG_ERROR_MSG("[MQTT] connect failed: %s", mosquitto_strerror(rc));
    }
    else
    {
        std::cout << "[MQTT] connected to " << m_host << ":" << m_port << '\n';
        LOG_INFO_MSG("[MQTT] connected to %s:%d", m_host.c_str(), m_port);
    }
    return rc;
}

void mqtt_client::disconnect()
{
    if (m_mosq)
    {
        mosquitto_disconnect(m_mosq);
    }
}

int mqtt_client::publish(const std::string& topic, const std::string& payload, int qos)
{
    int rc =
        mosquitto_publish(m_mosq, nullptr, topic.c_str(), static_cast<int>(payload.size()), payload.c_str(), qos, false);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        std::cerr << "[MQTT] publish to " << topic << " failed: " << mosquitto_strerror(rc) << '\n';
        LOG_ERROR_MSG("[MQTT] publish to %s failed: %s", topic.c_str(), mosquitto_strerror(rc));
    }
    return rc;
}

int mqtt_client::get_socket_fd() const
{
    return m_mosq ? mosquitto_socket(m_mosq) : -1;
}

int mqtt_client::on_readable()
{
    return mosquitto_loop_read(m_mosq, 1);
}

int mqtt_client::on_writable()
{
    return mosquitto_loop_write(m_mosq, 1);
}

int mqtt_client::loop_misc()
{
    return mosquitto_loop_misc(m_mosq);
}

bool mqtt_client::want_write() const
{
    return m_mosq && mosquitto_want_write(m_mosq);
}
