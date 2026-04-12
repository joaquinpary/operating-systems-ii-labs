#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/poll.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_WIFI)
#include <zephyr/net/wifi_mgmt.h>
#endif

#include "gps_telemetry.h"
#include "provisioning.h"

LOG_MODULE_REGISTER(dhl_courier, LOG_LEVEL_INF);

#define MQTT_BUFFER_SIZE 1024
#define MQTT_CONNECT_TIMEOUT_MS 5000
#define MQTT_POLL_SLICE_MS 1000
#define MQTT_HEARTBEAT_INTERVAL_MS 30000
#define WIFI_CONNECT_RETRIES 3
#define WIFI_CONNECT_TIMEOUT_S 30
#define NET_L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

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

static void prepare_fds(struct mqtt_client *client)
{
	mqtt_fds[0].fd = client->transport.tcp.sock;
	mqtt_fds[0].events = POLLIN;
	mqtt_fds[0].revents = 0;
	mqtt_nfds = 1;
}

static int wait_on_socket(int timeout_ms)
{
	if (mqtt_nfds == 0) {
		return 0;
	}

	return poll(mqtt_fds, mqtt_nfds, timeout_ms);
}

static int broker_init(struct sockaddr_storage *broker_addr)
{
	struct zsock_addrinfo hints = {0};
	struct zsock_addrinfo *result;
	char port[6];
	int rc;

	snprintf(port, sizeof(port), "%d", CONFIG_DHL_MQTT_BROKER_PORT);

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = zsock_getaddrinfo(CONFIG_DHL_MQTT_BROKER_HOSTNAME, port, &hints, &result);
	if (rc != 0) {
		LOG_ERR("Unable to resolve broker %s:%s (%s)",
			CONFIG_DHL_MQTT_BROKER_HOSTNAME, port, zsock_gai_strerror(rc));
		return -EHOSTUNREACH;
	}

	if (result->ai_addrlen > sizeof(*broker_addr)) {
		zsock_freeaddrinfo(result);
		return -ENOMEM;
	}

	memset(broker_addr, 0, sizeof(*broker_addr));
	memcpy(broker_addr, result->ai_addr, result->ai_addrlen);
	zsock_freeaddrinfo(result);

	return 0;
}

static void mqtt_evt_handler(struct mqtt_client *const client,
				     const struct mqtt_evt *evt)
{
	ARG_UNUSED(client);

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed (%d)", evt->result);
			break;
		}

		mqtt_connected = true;
		LOG_INF("MQTT connected to broker");
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

static int client_init(struct mqtt_client *client)
{
	int rc;

	rc = broker_init(&broker);
	if (rc != 0) {
		return rc;
	}

	mqtt_client_init(client);

	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)CONFIG_DHL_MQTT_CLIENT_ID;
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

static int mqtt_connect_and_wait(struct mqtt_client *client)
{
	int64_t deadline = k_uptime_get() + MQTT_CONNECT_TIMEOUT_MS;
	int rc;

	mqtt_connected = false;
	clear_fds();

	rc = client_init(client);
	if (rc != 0) {
		return rc;
	}

	rc = mqtt_connect(client);
	if (rc != 0) {
		LOG_ERR("mqtt_connect failed (%d)", rc);
		return rc;
	}

	prepare_fds(client);

	while (!mqtt_connected && k_uptime_get() < deadline) {
		rc = wait_on_socket(MQTT_POLL_SLICE_MS);
		if (rc < 0) {
			LOG_ERR("poll failed (%d)", errno);
			mqtt_abort(client);
			clear_fds();
			return -errno;
		}

		if (rc == 0) {
			continue;
		}

		rc = mqtt_input(client);
		if (rc != 0) {
			LOG_ERR("mqtt_input during connect failed (%d)", rc);
			mqtt_abort(client);
			clear_fds();
			return rc;
		}
	}

	if (!mqtt_connected) {
		LOG_ERR("Timed out waiting for MQTT CONNACK");
		mqtt_abort(client);
		clear_fds();
		return -ETIMEDOUT;
	}

	return 0;
}

static int publish_heartbeat(struct mqtt_client *client, const char *employee_id)
{
	char topic[64];
	char payload[64];

	snprintf(topic, sizeof(topic), "couriers/%s/status", employee_id);
	snprintf(payload, sizeof(payload), "{\"status\":\"online\",\"uptime\":%lld}",
		 (long long)(k_uptime_get() / 1000));

	struct mqtt_publish_param param = {
		.message = {
			.topic = {
				.topic = {
					.utf8 = (const uint8_t *)topic,
					.size = strlen(topic)
				},
				.qos = MQTT_QOS_0_AT_MOST_ONCE
			},
			.payload = {
				.data = (uint8_t *)payload,
				.len = strlen(payload)
			}
		},
		.message_id = 0,
		.dup_flag = 0U,
		.retain_flag = 0U
	};

	return mqtt_publish(client, &param);
}

static int mqtt_run(struct mqtt_client *client, const char *employee_id)
{
	int rc;
	int timeout_ms;
	int64_t last_heartbeat = k_uptime_get();

	/* Publish immediately on connect to confirm broker linkup */
	rc = publish_heartbeat(client, employee_id);
	if (rc != 0) {
		LOG_WRN("Initial heartbeat publish failed (%d)", rc);
	}

	while (mqtt_connected) {
		timeout_ms = mqtt_keepalive_time_left(client);
		if (timeout_ms < 0 || timeout_ms > MQTT_POLL_SLICE_MS) {
			timeout_ms = MQTT_POLL_SLICE_MS;
		}

		rc = wait_on_socket(timeout_ms);
		if (rc < 0) {
			LOG_ERR("poll failed (%d)", errno);
			return -errno;
		}

		if (rc > 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input failed (%d)", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_ERR("mqtt_live failed (%d)", rc);
			return rc;
		}

		if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				LOG_ERR("mqtt_input after mqtt_live failed (%d)", rc);
				return rc;
			}
		}

		/* Periodic heartbeat */
		if (k_uptime_get() - last_heartbeat >= MQTT_HEARTBEAT_INTERVAL_MS) {
			rc = publish_heartbeat(client, employee_id);
			if (rc != 0) {
				LOG_WRN("Heartbeat publish failed (%d)", rc);
			}
			last_heartbeat = k_uptime_get();
		}

		rc = gps_telemetry_publish(client, employee_id);
		if (rc != 0 && rc != -EAGAIN) {
			LOG_WRN("GPS telemetry publish failed (%d)", rc);
		}
	}

	return 0;
}

static void net_l4_event_handler(struct net_mgmt_event_callback *cb,
					 uint64_t mgmt_event,
					 struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	switch (mgmt_event) {
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

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
					   uint64_t mgmt_event,
					   struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = cb->info;

		wifi_assoc_result = status->status;
		if (status->status != 0) {
			LOG_ERR("Wi-Fi association failed (status %d)", status->status);
		} else {
			LOG_INF("Wi-Fi association completed");
		}
		k_sem_give(&wifi_assoc_sem);
	} else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected");
	}
}

static int wifi_connect(const struct prov_config *prov)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_connect_req_params params = {0};
	int rc;
	size_t ssid_len = strlen(prov->ssid);
	size_t psk_len  = strlen(prov->psk);

	/* Security types to try in order — only what the ESP32 driver supports */
	static const enum wifi_security_type sec_types[] = {
		WIFI_SECURITY_TYPE_PSK,
		WIFI_SECURITY_TYPE_PSK_SHA256,
	};
	const int n_sec = psk_len > 0U ? 2 : 1;

	if (iface == NULL) {
		return -ENODEV;
	}

	if (ssid_len == 0U) {
		LOG_ERR("SSID is empty");
		return -EINVAL;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT |
				     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);

	for (int s = 0; s < n_sec; s++) {
		params.ssid        = (const uint8_t *)prov->ssid;
		params.ssid_length = (uint8_t)ssid_len;
		params.channel     = WIFI_CHANNEL_ANY;
		params.band        = WIFI_FREQ_BAND_UNKNOWN;
		params.mfp         = WIFI_MFP_OPTIONAL;
		params.timeout     = SYS_FOREVER_MS;

		if (psk_len > 0U) {
			params.psk        = (const uint8_t *)prov->psk;
			params.psk_length = (uint8_t)psk_len;
			params.security   = sec_types[s];
		} else {
			params.psk        = NULL;
			params.psk_length = 0;
			params.security   = WIFI_SECURITY_TYPE_NONE;
		}

		for (int attempt = 0; attempt < 2; attempt++) {
			/* Ensure driver is in a clean state before each attempt */
			net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
			k_msleep(500);

			LOG_INF("Wi-Fi connect: SSID=\"%s\" sec=%d attempt %d/%d",
				prov->ssid, params.security, attempt + 1, 2);

			k_sem_reset(&wifi_assoc_sem);
			wifi_assoc_result = -1;

			rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
				      &params, sizeof(params));
			if (rc != 0) {
				LOG_WRN("Connect request rejected (%d)", rc);
				k_msleep(1000);
				continue;
			}

			rc = k_sem_take(&wifi_assoc_sem,
					K_SECONDS(WIFI_CONNECT_TIMEOUT_S));
			if (rc != 0) {
				LOG_WRN("Association timed out (sec=%d)",
					params.security);
				continue;
			}

			if (wifi_assoc_result == 0) {
				LOG_INF("Wi-Fi connected! (sec=%d)",
					params.security);
				return 0;
			}

			LOG_WRN("Association refused (status=%d, sec=%d)",
				wifi_assoc_result, params.security);
			k_msleep(1000);
		}
	}

	net_mgmt_del_event_callback(&wifi_cb);
	return -ECONNREFUSED;
}
#endif

static void wait_for_network(void)
{
	while (!network_ready) {
		if (k_sem_take(&network_ready_sem, K_SECONDS(1)) == 0) {
			break;
		}

		LOG_INF("Waiting for network connectivity...");
	}
}

int main(void)
{
	struct net_if *iface;
	int rc;

	clear_fds();

	/* Initialise settings (NVS backend for provisioning) */
	rc = settings_subsys_init();
	if (rc != 0) {
		LOG_ERR("settings_subsys_init failed (%d)", rc);
		return rc;
	}

	settings_load();
	gps_telemetry_init();

	iface = net_if_get_default();
	if (iface == NULL) {
		LOG_ERR("No network interface available");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&net_l4_cb, net_l4_event_handler, NET_L4_EVENT_MASK);
	net_mgmt_add_event_callback(&net_l4_cb);

#if defined(CONFIG_WIFI)
	/* ── Provisioning ─────────────────────────────────────────────────── */
	rc = prov_load(&g_prov);
	if (rc != 0) {
		LOG_ERR("No credentials available – set CONFIG_DHL_WIFI_SSID in prj.conf");
		return rc;
	}

	/* ── Connect to the saved Wi-Fi network ───────────────────────────── */
	rc = wifi_connect(&g_prov);
	if (rc != 0) {
		LOG_ERR("Wi-Fi connect failed (%d) – erasing credentials and rebooting", rc);
		prov_erase();
		k_sleep(K_SECONDS(1));
		sys_reboot(SYS_REBOOT_COLD);
		return rc;
	}
#endif

	conn_mgr_mon_resend_status();
	wait_for_network();

	LOG_INF("Courier %s starting MQTT bootstrap", g_prov.employee_id);

	while (1) {
		if (!network_ready) {
			wait_for_network();
		}

		rc = mqtt_connect_and_wait(&client_ctx);
		if (rc != 0) {
			LOG_WRN("MQTT connect attempt failed (%d), retrying in %d seconds",
				rc, CONFIG_DHL_MQTT_RECONNECT_DELAY);
			k_sleep(K_SECONDS(CONFIG_DHL_MQTT_RECONNECT_DELAY));
			continue;
		}

		gps_telemetry_start();
		rc = mqtt_run(&client_ctx, g_prov.employee_id);
		if (rc != 0) {
			LOG_WRN("MQTT loop ended with %d", rc);
		}

		gps_telemetry_stop();
		mqtt_disconnect(&client_ctx, NULL);
		mqtt_abort(&client_ctx);
		clear_fds();
		k_sleep(K_SECONDS(CONFIG_DHL_MQTT_RECONNECT_DELAY));
	}

	return 0;
}