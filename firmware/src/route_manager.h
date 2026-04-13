#ifndef ROUTE_MANAGER_H_
#define ROUTE_MANAGER_H_

#include <stddef.h>

#include <autoconf.h>

struct route_state {
	char stops[CONFIG_DHL_ROUTE_MAX_STOPS][CONFIG_DHL_ROUTE_STOP_NAME_MAX];
	int count;
	int current_index;
};

/**
 * @brief Initialise the route manager (zeroes internal state).
 */
void route_manager_init(void);

/**
 * @brief Append stops to the current route.
 *
 * New stops are added after the last existing stop without resetting the
 * current position. To reset, publish an empty array ([]) which calls
 * route_manager_clear() internally.
 *
 * @param stops  Array of stop name pointers.
 * @param count  Number of stops to append (clamped to remaining free slots).
 */
void route_manager_update(const char *stops[], int count);

/**
 * @brief Get the name of the current stop.
 *
 * @return Pointer to the stop name, or NULL if the route is empty / exhausted.
 */
const char *get_current_stop(void);

/**
 * @brief Get the zero-based index of the current stop.
 *
 * @return Index of the active stop, or -1 if the route is empty/exhausted.
 */
int get_current_stop_index(void);

/**
 * @brief Return the total number of stops in the loaded route.
 */
int get_stop_count(void);

/**
 * @brief Advance to the next stop.
 *
 * @return 0 on success, -1 if the route is empty or already exhausted.
 */
int advance_stop(void);

/**
 * @brief Parse a JSON array of stop names and update the route.
 *
 * Expected format: ["Stop A", "Stop B", ...]
 * On parse failure the existing route is left untouched.
 *
 * @param json  Pointer to the JSON payload buffer.
 * @param len   Length of the JSON payload in bytes.
 * @return 0 on success, -EINVAL on malformed JSON or unexpected types.
 */
int route_manager_parse_json(const char *json, size_t len);

/**
 * @brief Clear the current route entirely.
 */
void route_manager_clear(void);

#endif /* ROUTE_MANAGER_H_ */
