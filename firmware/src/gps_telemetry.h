#ifndef GPS_TELEMETRY_H_
#define GPS_TELEMETRY_H_

#include <stdint.h>

#include <zephyr/net/mqtt.h>

#define GPS_TELEMETRY_INTERVAL_SECONDS 5
#define GPS_BASE_LAT_MICRODEGREES (-31416600)
#define GPS_BASE_LNG_MICRODEGREES (-64183300)
#define GPS_RANDOM_STEP_MICRODEGREES 100

void gps_telemetry_init(void);
void gps_telemetry_start(void);
void gps_telemetry_stop(void);
void gps_telemetry_get_position(int32_t* lat_microdegrees, int32_t* lng_microdegrees);
int gps_telemetry_publish(struct mqtt_client* client, const char* employee_id);

#endif /* GPS_TELEMETRY_H_ */
