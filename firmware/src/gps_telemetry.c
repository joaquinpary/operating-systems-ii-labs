#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>

#include "gps_telemetry.h"

LOG_MODULE_REGISTER(gps_telemetry, LOG_LEVEL_INF);

#define GPS_TOPIC_SIZE 64
#define GPS_COORD_SIZE 16
#define GPS_PAYLOAD_SIZE 128

static struct k_timer gps_timer;
static struct k_work gps_work;
static struct k_mutex gps_lock;
static atomic_t gps_pending;
static atomic_t gps_running;
static int32_t current_lat_microdegrees = GPS_BASE_LAT_MICRODEGREES;
static int32_t current_lng_microdegrees = GPS_BASE_LNG_MICRODEGREES;

static int32_t random_delta_microdegrees(void)
{
	uint32_t span = (GPS_RANDOM_STEP_MICRODEGREES * 2U) + 1U;

	return (int32_t)(sys_rand32_get() % span) - GPS_RANDOM_STEP_MICRODEGREES;
}

static void format_coordinate(char *buf, size_t size, int32_t microdegrees)
{
	int64_t absolute_value = (microdegrees < 0) ? -(int64_t)microdegrees : (int64_t)microdegrees;
	int64_t integer_part = absolute_value / 1000000LL;
	int64_t fractional_part = absolute_value % 1000000LL;

	snprintf(buf, size, "%s%lld.%06lld",
		 microdegrees < 0 ? "-" : "",
		 (long long)integer_part,
		 (long long)fractional_part);
}

static void gps_work_handler(struct k_work *work)
{
	int32_t next_lat;
	int32_t next_lng;

	ARG_UNUSED(work);

	if (atomic_get(&gps_running) == 0) {
		return;
	}

	k_mutex_lock(&gps_lock, K_FOREVER);
	current_lat_microdegrees += random_delta_microdegrees();
	current_lng_microdegrees += random_delta_microdegrees();
	next_lat = current_lat_microdegrees;
	next_lng = current_lng_microdegrees;
	k_mutex_unlock(&gps_lock);

	atomic_set(&gps_pending, 1);
	LOG_DBG("GPS updated to %d,%d microdegrees", next_lat, next_lng);
}

static void gps_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (atomic_get(&gps_running) != 0) {
		k_work_submit(&gps_work);
	}
}

void gps_telemetry_init(void)
{
	k_mutex_init(&gps_lock);
	k_work_init(&gps_work, gps_work_handler);
	k_timer_init(&gps_timer, gps_timer_handler, NULL);
	atomic_clear(&gps_pending);
	atomic_clear(&gps_running);
	current_lat_microdegrees = GPS_BASE_LAT_MICRODEGREES;
	current_lng_microdegrees = GPS_BASE_LNG_MICRODEGREES;
}

void gps_telemetry_start(void)
{
	atomic_clear(&gps_pending);
	atomic_set(&gps_running, 1);
	k_timer_start(&gps_timer,
		      K_SECONDS(GPS_TELEMETRY_INTERVAL_SECONDS),
		      K_SECONDS(GPS_TELEMETRY_INTERVAL_SECONDS));
}

void gps_telemetry_stop(void)
{
	atomic_clear(&gps_running);
	k_timer_stop(&gps_timer);
	atomic_clear(&gps_pending);
}

void gps_telemetry_get_position(int32_t *lat_microdegrees,
				      int32_t *lng_microdegrees)
{
	k_mutex_lock(&gps_lock, K_FOREVER);

	if (lat_microdegrees != NULL) {
		*lat_microdegrees = current_lat_microdegrees;
	}

	if (lng_microdegrees != NULL) {
		*lng_microdegrees = current_lng_microdegrees;
	}

	k_mutex_unlock(&gps_lock);
}

int gps_telemetry_publish(struct mqtt_client *client, const char *employee_id)
{
	char topic[GPS_TOPIC_SIZE];
	char latitude[GPS_COORD_SIZE];
	char longitude[GPS_COORD_SIZE];
	char payload[GPS_PAYLOAD_SIZE];
	int32_t lat_snapshot;
	int32_t lng_snapshot;
	int rc;

	if (!atomic_cas(&gps_pending, 1, 0)) {
		return -EAGAIN;
	}

	k_mutex_lock(&gps_lock, K_FOREVER);
	lat_snapshot = current_lat_microdegrees;
	lng_snapshot = current_lng_microdegrees;
	k_mutex_unlock(&gps_lock);

	format_coordinate(latitude, sizeof(latitude), lat_snapshot);
	format_coordinate(longitude, sizeof(longitude), lng_snapshot);

	rc = snprintf(topic, sizeof(topic), "tracking/%s", employee_id);
	if (rc < 0 || rc >= sizeof(topic)) {
		return -EMSGSIZE;
	}

	rc = snprintf(payload, sizeof(payload),
		      "{\"lat\":%s,\"lng\":%s}",
		      latitude, longitude);
	if (rc < 0 || rc >= sizeof(payload)) {
		return -EMSGSIZE;
	}

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