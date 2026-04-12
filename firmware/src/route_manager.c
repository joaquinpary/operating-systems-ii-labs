#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "route_manager.h"

LOG_MODULE_REGISTER(route_manager, LOG_LEVEL_INF);

static struct route_state state;
static K_MUTEX_DEFINE(route_lock);

void route_manager_init(void)
{
	k_mutex_lock(&route_lock, K_FOREVER);
	memset(&state, 0, sizeof(state));
	k_mutex_unlock(&route_lock);
}

void route_manager_update(const char *stops[], int count)
{
	k_mutex_lock(&route_lock, K_FOREVER);

	if (count > CONFIG_DHL_ROUTE_MAX_STOPS) {
		count = CONFIG_DHL_ROUTE_MAX_STOPS;
	}

	memset(&state, 0, sizeof(state));

	for (int i = 0; i < count; i++) {
		strncpy(state.stops[i], stops[i], CONFIG_DHL_ROUTE_STOP_NAME_MAX - 1);
		state.stops[i][CONFIG_DHL_ROUTE_STOP_NAME_MAX - 1] = '\0';
	}

	state.count = count;
	state.current_index = 0;

	LOG_INF("Route updated: %d stop(s), first=\"%s\"",
		count, count > 0 ? state.stops[0] : "(none)");

	k_mutex_unlock(&route_lock);
}

const char *get_current_stop(void)
{
	const char *name = NULL;

	k_mutex_lock(&route_lock, K_FOREVER);

	if (state.count > 0 && state.current_index < state.count) {
		name = state.stops[state.current_index];
	}

	k_mutex_unlock(&route_lock);
	return name;
}

int get_stop_count(void)
{
	int count;

	k_mutex_lock(&route_lock, K_FOREVER);
	count = state.count;
	k_mutex_unlock(&route_lock);

	return count;
}

int advance_stop(void)
{
	int rc = -1;

	k_mutex_lock(&route_lock, K_FOREVER);

	if (state.count > 0 && state.current_index < state.count - 1) {
		state.current_index++;
		LOG_INF("Advanced to stop %d/%d: \"%s\"",
			state.current_index + 1, state.count,
			state.stops[state.current_index]);
		rc = 0;
	}

	k_mutex_unlock(&route_lock);
	return rc;
}

void route_manager_clear(void)
{
	k_mutex_lock(&route_lock, K_FOREVER);
	memset(&state, 0, sizeof(state));
	LOG_INF("Route cleared");
	k_mutex_unlock(&route_lock);
}
