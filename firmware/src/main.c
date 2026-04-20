#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/poll.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_WIFI)
#include <zephyr/net/wifi_mgmt.h>
#endif

#include "gps_telemetry.h"
#include "provisioning.h"
#include "route_manager.h"
#include "ui_bridge.h"

LOG_MODULE_REGISTER(dhl_courier, LOG_LEVEL_INF);

#define MQTT_BUFFER_SIZE 1024
#define MQTT_CONNECT_TIMEOUT_MS 5000
#define MQTT_POLL_SLICE_MS 1000
#define WIFI_CONNECT_RETRIES 3
#define WIFI_CONNECT_TIMEOUT_S 30
#define NET_L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

/* Topic prefix for route assignments from the backend */
#define ROUTE_TOPIC_PREFIX "routes/"
#define ALERTS_SOS_TOPIC_PREFIX "alerts/sos/"
#define DELIVERED_TOPIC_PREFIX "delivered/"
#define MQTT_TOPIC_SIZE 96
#define MQTT_PAYLOAD_SIZE 192
#define MQTT_TIMESTAMP_SIZE 32
#define MQTT_COORDINATE_SIZE 16
#define UI_SLEEP_SLICE_MS 100

/* Next MQTT message id for subscribe / QoS-1 ack (wraps at UINT16_MAX) */
static uint16_t next_msg_id = 1U;

static uint8_t rx_buffer[MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[MQTT_BUFFER_SIZE];

static struct mqtt_client client_ctx;
static struct sockaddr_storage broker;
static struct pollfd mqtt_fds[1];
static struct net_mgmt_event_callback net_l4_cb;

#if defined(CONFIG_WIFI)
static struct net_mgmt_event_callback wifi_cb;
#endif

static int mqtt_nfds;
static bool mqtt_connected;
static bool network_ready;

/* Runtime provisioning config (loaded from NVS or set via captive portal) */
static struct prov_config g_prov;

K_SEM_DEFINE(network_ready_sem, 0, 1);

static void clear_fds(void)
{
    mqtt_nfds = 0;
    mqtt_fds[0].fd = -1;
    mqtt_fds[0].events = 0;
    mqtt_fds[0].revents = 0;
}

static void prepare_fds(struct mqtt_client* client)
{
    mqtt_fds[0].fd = client->transport.tcp.sock;
    mqtt_fds[0].events = POLLIN;
    mqtt_fds[0].revents = 0;
    mqtt_nfds = 1;
}

static int wait_on_socket(int timeout_ms)
{
    if (mqtt_nfds == 0)
    {
        return 0;
    }

    return poll(mqtt_fds, mqtt_nfds, timeout_ms);
}

static void format_coordinate(char* buf, size_t size, int32_t microdegrees)
{
    int64_t absolute_value = (microdegrees < 0) ? -(int64_t)microdegrees : (int64_t)microdegrees;
    int64_t integer_part = absolute_value / 1000000LL;
    int64_t fractional_part = absolute_value % 1000000LL;

    snprintf(buf, size, "%s%lld.%06lld", microdegrees < 0 ? "-" : "", (long long)integer_part,
             (long long)fractional_part);
}

static void refresh_ui_stop(void)
{
    int current_index = get_current_stop_index();
    int display_position = current_index >= 0 ? current_index + 1 : 0;

    ui_update_stop(get_current_stop(), display_position, get_stop_count());
}

static void ui_pump_sleep(int32_t duration_ms)
{
    while (duration_ms > 0)
    {
        int32_t slice_ms = duration_ms > UI_SLEEP_SLICE_MS ? UI_SLEEP_SLICE_MS : duration_ms;

        ui_process();
        k_msleep(slice_ms);
        duration_ms -= slice_ms;
    }
}

static int publish_json(struct mqtt_client* client, const char* topic, const char* payload)
{
    struct mqtt_publish_param param = {
        .message = {.topic = {.topic = {.utf8 = (const uint8_t*)topic, .size = strlen(topic)},
                              .qos = MQTT_QOS_0_AT_MOST_ONCE},
                    .payload = {.data = (uint8_t*)payload, .len = strlen(payload)}},
        .message_id = 0,
        .dup_flag = 0U,
        .retain_flag = 0U};

    return mqtt_publish(client, &param);
}

static int publish_employee_event(const char* topic_prefix, const char* payload)
{
    char topic[MQTT_TOPIC_SIZE];
    int rc;

    if (!mqtt_connected)
    {
        return -ENOTCONN;
    }

    rc = snprintf(topic, sizeof(topic), "%s%s", topic_prefix, g_prov.employee_id);
    if (rc < 0 || rc >= sizeof(topic))
    {
        return -EMSGSIZE;
    }

    return publish_json(&client_ctx, topic, payload);
}

static int build_iso8601_utc(char* buf, size_t size)
{
    time_t now = time(NULL);
    struct tm utc_now;

    if (now == (time_t)-1)
    {
        return -EINVAL;
    }

    if (gmtime_r(&now, &utc_now) == NULL)
    {
        return -EINVAL;
    }

    if (strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &utc_now) == 0U)
    {
        return -EMSGSIZE;
    }

    return 0;
}

static void on_sos_pressed(void)
{
    int32_t lat_microdegrees;
    int32_t lng_microdegrees;
    char latitude[MQTT_COORDINATE_SIZE];
    char longitude[MQTT_COORDINATE_SIZE];
    char timestamp[MQTT_TIMESTAMP_SIZE];
    char payload[MQTT_PAYLOAD_SIZE];
    int rc;

    gps_telemetry_get_position(&lat_microdegrees, &lng_microdegrees);
    format_coordinate(latitude, sizeof(latitude), lat_microdegrees);
    format_coordinate(longitude, sizeof(longitude), lng_microdegrees);

    rc = build_iso8601_utc(timestamp, sizeof(timestamp));
    if (rc != 0)
    {
        LOG_WRN("Unable to build SOS timestamp (%d)", rc);
        return;
    }

    rc = snprintf(payload, sizeof(payload), "{\"time\":\"%s\",\"coordinates\":{\"lat\":%s,\"lon\":%s}}", timestamp,
                  latitude, longitude);
    if (rc < 0 || rc >= sizeof(payload))
    {
        LOG_WRN("SOS payload too large");
        return;
    }

    rc = publish_employee_event(ALERTS_SOS_TOPIC_PREFIX, payload);
    if (rc != 0)
    {
        LOG_WRN("Failed to publish SOS alert (%d)", rc);
        return;
    }

    LOG_INF("SOS alert published for %s", g_prov.employee_id);
}

static void on_delivered_pressed(void)
{
    const char* current_stop = get_current_stop();
    char payload[MQTT_PAYLOAD_SIZE];
    int rc;

    if (current_stop == NULL)
    {
        LOG_WRN("Delivered pressed without an active stop");
        return;
    }

    rc = snprintf(payload, sizeof(payload), "{\"stop\":\"%s\",\"status\":\"done\"}", current_stop);
    if (rc < 0 || rc >= sizeof(payload))
    {
        LOG_WRN("Delivered payload too large");
        return;
    }

    rc = publish_employee_event(DELIVERED_TOPIC_PREFIX, payload);
    if (rc != 0)
    {
        LOG_WRN("Failed to publish delivery confirmation (%d)", rc);
        return;
    }

    if (advance_stop() != 0)
    {
        LOG_WRN("Unable to advance route after delivery confirmation");
    }

    refresh_ui_stop();
    LOG_INF("Delivery confirmation published for \"%s\"", current_stop);
}

static int broker_init(struct sockaddr_storage* broker_addr)
{
    struct zsock_addrinfo hints = {0};
    struct zsock_addrinfo* result;
    char port[6];
    int rc;

    snprintf(port, sizeof(port), "%d", CONFIG_DHL_MQTT_BROKER_PORT);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    rc = zsock_getaddrinfo(CONFIG_DHL_MQTT_BROKER_HOSTNAME, port, &hints, &result);
    if (rc != 0)
    {
        LOG_ERR("Unable to resolve broker %s:%s (%s)", CONFIG_DHL_MQTT_BROKER_HOSTNAME, port, zsock_gai_strerror(rc));
        return -EHOSTUNREACH;
    }

    if (result->ai_addrlen > sizeof(*broker_addr))
    {
        zsock_freeaddrinfo(result);
        return -ENOMEM;
    }

    memset(broker_addr, 0, sizeof(*broker_addr));
    memcpy(broker_addr, result->ai_addr, result->ai_addrlen);
    zsock_freeaddrinfo(result);

    return 0;
}

static int subscribe_to_topics(struct mqtt_client* client)
{
    static char route_topic[sizeof(ROUTE_TOPIC_PREFIX) + sizeof(g_prov.employee_id)];

    snprintf(route_topic, sizeof(route_topic), "%s%s", ROUTE_TOPIC_PREFIX, g_prov.employee_id);

    struct mqtt_topic topics[] = {
        {
            .topic =
                {
                    .utf8 = (const uint8_t*)route_topic,
                    .size = strlen(route_topic),
                },
            .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        },
    };

    struct mqtt_subscription_list sub_list = {
        .list = topics,
        .list_count = ARRAY_SIZE(topics),
        .message_id = next_msg_id++,
    };

    int rc = mqtt_subscribe(client, &sub_list);

    if (rc != 0)
    {
        LOG_ERR("mqtt_subscribe failed (%d)", rc);
    }
    else
    {
        LOG_INF("Subscribed to %s (msg_id %u)", route_topic, sub_list.message_id);
    }

    return rc;
}

static int handle_publish(struct mqtt_client* client, const struct mqtt_evt* evt)
{
    const struct mqtt_publish_param* pub = &evt->param.publish;
    const struct mqtt_topic* topic = &pub->message.topic;
    uint32_t payload_len = pub->message.payload.len;
    int rc;

    /* Read payload into a stack buffer (bounded by MQTT_BUFFER_SIZE) */
    uint8_t payload[MQTT_BUFFER_SIZE];
    size_t to_read = payload_len;

    if (to_read > sizeof(payload))
    {
        LOG_WRN("Publish payload too large (%u bytes), truncating", payload_len);
        to_read = sizeof(payload);
    }

    rc = mqtt_read_publish_payload(client, payload, to_read);
    if (rc < 0)
    {
        LOG_ERR("Failed to read MQTT payload (%d)", rc);
        return rc;
    }

    size_t received = (size_t)rc;

    /* Discard remaining bytes if payload was truncated */
    if (payload_len > to_read)
    {
        uint8_t discard;
        size_t remaining = payload_len - to_read;

        while (remaining > 0)
        {
            rc = mqtt_read_publish_payload(client, &discard, 1);
            if (rc <= 0)
            {
                break;
            }
            remaining--;
        }
    }

    /* ACK QoS 1 messages */
    if (topic->qos == MQTT_QOS_1_AT_LEAST_ONCE)
    {
        struct mqtt_puback_param ack = {
            .message_id = pub->message_id,
        };

        (void)mqtt_publish_qos1_ack(client, &ack);
    }

    /* Route topic? "routes/{employee_id}" */
    size_t prefix_len = strlen(ROUTE_TOPIC_PREFIX);

    if (topic->topic.size >= prefix_len && memcmp(topic->topic.utf8, ROUTE_TOPIC_PREFIX, prefix_len) == 0)
    {
        rc = route_manager_parse_json((const char*)payload, received);
        if (rc != 0)
        {
            LOG_WRN("Route JSON rejected (%d)", rc);
        }
        else
        {
            refresh_ui_stop();
        }
    }
    else
    {
        LOG_DBG("Unhandled topic: %.*s", (int)topic->topic.size, topic->topic.utf8);
    }

    return 0;
}

static void mqtt_evt_handler(struct mqtt_client* const client, const struct mqtt_evt* evt)
{
    switch (evt->type)
    {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0)
        {
            LOG_ERR("MQTT connect failed (%d)", evt->result);
            break;
        }

        mqtt_connected = true;
        LOG_INF("MQTT connected to broker");

        /* Subscribe after every (re)connect so the broker
         * knows our topic interest even after a clean session. */
        subscribe_to_topics(client);
        break;

    case MQTT_EVT_SUBACK:
        LOG_INF("MQTT SUBACK received (msg_id %u)", evt->param.suback.message_id);
        break;

    case MQTT_EVT_PUBLISH:
        (void)handle_publish(client, evt);
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected (%d)", evt->result);
        mqtt_connected = false;
        clear_fds();
        break;

    case MQTT_EVT_PINGRESP:
        LOG_DBG("MQTT PINGRESP received");
        break;

    default:
        LOG_DBG("Unhandled MQTT event type %d result %d", evt->type, evt->result);
        break;
    }
}

static int client_init(struct mqtt_client* client)
{
    int rc;

    rc = broker_init(&broker);
    if (rc != 0)
    {
        return rc;
    }

    mqtt_client_init(client);

    client->broker = &broker;
    client->evt_cb = mqtt_evt_handler;
    client->client_id.utf8 = (uint8_t*)CONFIG_DHL_MQTT_CLIENT_ID;
    client->client_id.size = strlen(CONFIG_DHL_MQTT_CLIENT_ID);
    client->password = NULL;
    client->user_name = NULL;
    client->protocol_version = MQTT_VERSION_3_1_1;
    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);
    client->transport.type = MQTT_TRANSPORT_NON_SECURE;
    client->keepalive = CONFIG_DHL_MQTT_KEEPALIVE;
    client->clean_session = 1U;

    return 0;
}

static int mqtt_connect_and_wait(struct mqtt_client* client)
{
    int64_t deadline = k_uptime_get() + MQTT_CONNECT_TIMEOUT_MS;
    int rc;

    mqtt_connected = false;
    clear_fds();

    rc = client_init(client);
    if (rc != 0)
    {
        return rc;
    }

    rc = mqtt_connect(client);
    if (rc != 0)
    {
        LOG_ERR("mqtt_connect failed (%d)", rc);
        return rc;
    }

    prepare_fds(client);

    while (!mqtt_connected && k_uptime_get() < deadline)
    {
        ui_process();

        rc = wait_on_socket(MQTT_POLL_SLICE_MS);
        if (rc < 0)
        {
            LOG_ERR("poll failed (%d)", errno);
            mqtt_abort(client);
            clear_fds();
            return -errno;
        }

        if (rc == 0)
        {
            continue;
        }

        rc = mqtt_input(client);
        if (rc != 0)
        {
            LOG_ERR("mqtt_input during connect failed (%d)", rc);
            mqtt_abort(client);
            clear_fds();
            return rc;
        }
    }

    if (!mqtt_connected)
    {
        LOG_ERR("Timed out waiting for MQTT CONNACK");
        mqtt_abort(client);
        clear_fds();
        return -ETIMEDOUT;
    }

    return 0;
}

static void sync_time_sntp(void)
{
    struct sntp_time sntp_ts;
    struct timespec ts;
    int rc;

    rc = sntp_simple("pool.ntp.org", 5000, &sntp_ts);
    if (rc != 0)
    {
        LOG_WRN("SNTP sync failed (%d) - timestamps may be inaccurate", rc);
        return;
    }

    ts.tv_sec = (time_t)sntp_ts.seconds;
    ts.tv_nsec = 0;

    if (clock_settime(CLOCK_REALTIME, &ts) != 0)
    {
        LOG_WRN("clock_settime failed");
        return;
    }

    LOG_INF("Time synced via SNTP");
}

static int mqtt_run(struct mqtt_client* client, const char* employee_id)
{
    int rc;
    int timeout_ms;

    while (mqtt_connected)
    {
        timeout_ms = mqtt_keepalive_time_left(client);
        if (timeout_ms < 0 || timeout_ms > MQTT_POLL_SLICE_MS)
        {
            timeout_ms = MQTT_POLL_SLICE_MS;
        }

        rc = wait_on_socket(timeout_ms);
        if (rc < 0)
        {
            LOG_ERR("poll failed (%d)", errno);
            return -errno;
        }

        if (rc > 0)
        {
            rc = mqtt_input(client);
            if (rc != 0)
            {
                LOG_ERR("mqtt_input failed (%d)", rc);
                return rc;
            }
        }

        rc = mqtt_live(client);
        if (rc != 0 && rc != -EAGAIN)
        {
            LOG_ERR("mqtt_live failed (%d)", rc);
            return rc;
        }

        if (rc == 0)
        {
            rc = mqtt_input(client);
            if (rc != 0)
            {
                LOG_ERR("mqtt_input after mqtt_live failed (%d)", rc);
                return rc;
            }
        }

        rc = gps_telemetry_publish(client, employee_id);
        if (rc != 0 && rc != -EAGAIN)
        {
            LOG_WRN("GPS telemetry publish failed (%d)", rc);
        }

        ui_process();
    }

    return 0;
}

static void net_l4_event_handler(struct net_mgmt_event_callback* cb, uint64_t mgmt_event, struct net_if* iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    switch (mgmt_event)
    {
    case NET_EVENT_L4_CONNECTED:
        network_ready = true;
        k_sem_give(&network_ready_sem);
        LOG_INF("Network connectivity up");
        break;

    case NET_EVENT_L4_DISCONNECTED:
        network_ready = false;
        LOG_WRN("Network connectivity down");
        break;

    default:
        break;
    }
}

#if defined(CONFIG_WIFI)
static K_SEM_DEFINE(wifi_assoc_sem, 0, 1);
static int wifi_assoc_result;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback* cb, uint64_t mgmt_event, struct net_if* iface)
{
    ARG_UNUSED(iface);

    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT)
    {
        const struct wifi_status* status = cb->info;

        wifi_assoc_result = status->status;
        if (status->status != 0)
        {
            LOG_ERR("Wi-Fi association failed (status %d)", status->status);
        }
        else
        {
            LOG_INF("Wi-Fi association completed");
        }
        k_sem_give(&wifi_assoc_sem);
    }
    else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT)
    {
        LOG_WRN("Wi-Fi disconnected");
    }
}

static int wifi_connect(const struct prov_config* prov)
{
    struct net_if* iface = net_if_get_default();
    struct wifi_connect_req_params params = {0};
    int rc;
    size_t ssid_len = strlen(prov->ssid);
    size_t psk_len = strlen(prov->psk);

    /* Security types to try in order — only what the ESP32 driver supports */
    static const enum wifi_security_type sec_types[] = {
        WIFI_SECURITY_TYPE_PSK,
        WIFI_SECURITY_TYPE_PSK_SHA256,
    };
    const int n_sec = psk_len > 0U ? 2 : 1;

    if (iface == NULL)
    {
        return -ENODEV;
    }

    if (ssid_len == 0U)
    {
        LOG_ERR("SSID is empty");
        return -EINVAL;
    }

    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    for (int s = 0; s < n_sec; s++)
    {
        params.ssid = (const uint8_t*)prov->ssid;
        params.ssid_length = (uint8_t)ssid_len;
        params.channel = WIFI_CHANNEL_ANY;
        params.band = WIFI_FREQ_BAND_UNKNOWN;
        params.mfp = WIFI_MFP_OPTIONAL;
        params.timeout = SYS_FOREVER_MS;

        if (psk_len > 0U)
        {
            params.psk = (const uint8_t*)prov->psk;
            params.psk_length = (uint8_t)psk_len;
            params.security = sec_types[s];
        }
        else
        {
            params.psk = NULL;
            params.psk_length = 0;
            params.security = WIFI_SECURITY_TYPE_NONE;
        }

        for (int attempt = 0; attempt < 2; attempt++)
        {
            /* Ensure driver is in a clean state before each attempt */
            net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
            k_msleep(500);

            LOG_INF("Wi-Fi connect: SSID=\"%s\" sec=%d attempt %d/%d", prov->ssid, params.security, attempt + 1, 2);

            k_sem_reset(&wifi_assoc_sem);
            wifi_assoc_result = -1;

            rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
            if (rc != 0)
            {
                LOG_WRN("Connect request rejected (%d)", rc);
                k_msleep(1000);
                continue;
            }

            rc = k_sem_take(&wifi_assoc_sem, K_SECONDS(WIFI_CONNECT_TIMEOUT_S));
            if (rc != 0)
            {
                LOG_WRN("Association timed out (sec=%d)", params.security);
                continue;
            }

            if (wifi_assoc_result == 0)
            {
                LOG_INF("Wi-Fi connected! (sec=%d)", params.security);
                return 0;
            }

            LOG_WRN("Association refused (status=%d, sec=%d)", wifi_assoc_result, params.security);
            k_msleep(1000);
        }
    }

    net_mgmt_del_event_callback(&wifi_cb);
    return -ECONNREFUSED;
}
#endif

static void wait_for_network(void)
{
    int64_t last_log_ms = 0;

    while (!network_ready)
    {
        ui_process();

        if (k_sem_take(&network_ready_sem, K_MSEC(UI_SLEEP_SLICE_MS)) == 0)
        {
            break;
        }

        if (k_uptime_get() - last_log_ms >= 1000)
        {
            LOG_INF("Waiting for network connectivity...");
            last_log_ms = k_uptime_get();
        }
    }
}

int main(void)
{
    struct net_if* iface;
    int rc;

    clear_fds();

    /* Initialise settings (NVS backend for provisioning) */
    rc = settings_subsys_init();
    if (rc != 0)
    {
        LOG_ERR("settings_subsys_init failed (%d)", rc);
        return rc;
    }

    settings_load();
    gps_telemetry_init();
    route_manager_init();
    rc = prov_load(&g_prov);
    if (rc != 0)
    {
        LOG_ERR("No provisioning available – set CONFIG_DHL_EMPLOYEE_ID and Wi-Fi creds when needed");
        ui_shutdown();
        return rc;
    }

    ui_init();
    ui_register_sos_callback(on_sos_pressed);
    ui_register_delivered_callback(on_delivered_pressed);
    refresh_ui_stop();

    iface = net_if_get_default();
    if (iface == NULL)
    {
        LOG_ERR("No network interface available");
        ui_shutdown();
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&net_l4_cb, net_l4_event_handler, NET_L4_EVENT_MASK);
    net_mgmt_add_event_callback(&net_l4_cb);

#if defined(CONFIG_WIFI)
    /* ── Connect to the saved Wi-Fi network ───────────────────────────── */
    rc = wifi_connect(&g_prov);
    if (rc != 0)
    {
        LOG_ERR("Wi-Fi connect failed (%d) – erasing credentials and rebooting", rc);
        prov_erase();
        k_sleep(K_SECONDS(1));
        sys_reboot(SYS_REBOOT_COLD);
        return rc;
    }
#endif

#if defined(CONFIG_WIFI)
    conn_mgr_mon_resend_status();
    wait_for_network();
#else
    /* On native_sim the host network stack is always available;
     * there is no Wi-Fi association event to await. */
    network_ready = true;
#endif

    LOG_INF("Courier %s starting MQTT bootstrap", g_prov.employee_id);

    sync_time_sntp();

    while (1)
    {
        if (!network_ready)
        {
            wait_for_network();
        }

        rc = mqtt_connect_and_wait(&client_ctx);
        if (rc != 0)
        {
            LOG_WRN("MQTT connect attempt failed (%d), retrying in %d seconds", rc, CONFIG_DHL_MQTT_RECONNECT_DELAY);
            ui_pump_sleep(CONFIG_DHL_MQTT_RECONNECT_DELAY * MSEC_PER_SEC);
            continue;
        }

        gps_telemetry_start();
        rc = mqtt_run(&client_ctx, g_prov.employee_id);
        if (rc != 0)
        {
            LOG_WRN("MQTT loop ended with %d", rc);
        }

        gps_telemetry_stop();
        mqtt_disconnect(&client_ctx, NULL);
        mqtt_abort(&client_ctx);
        clear_fds();
        ui_pump_sleep(CONFIG_DHL_MQTT_RECONNECT_DELAY * MSEC_PER_SEC);
    }

    ui_shutdown();
    return 0;
}
