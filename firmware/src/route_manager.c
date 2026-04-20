#include <errno.h>
#include <string.h>

#include <cJSON.h>
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

void route_manager_update(const char* stops[], int count)
{
    k_mutex_lock(&route_lock, K_FOREVER);

    int space = CONFIG_DHL_ROUTE_MAX_STOPS - state.count;

    if (count > space)
    {
        LOG_WRN("Route append: %d stop(s) requested, only %d slot(s) free; clamping", count, space);
        count = space;
    }

    for (int i = 0; i < count; i++)
    {
        strncpy(state.stops[state.count + i], stops[i], CONFIG_DHL_ROUTE_STOP_NAME_MAX - 1);
        state.stops[state.count + i][CONFIG_DHL_ROUTE_STOP_NAME_MAX - 1] = '\0';
    }

    state.count += count;

    LOG_INF("Route append: +%d stop(s), total=%d, current=%d/%d", count, state.count, state.current_index + 1,
            state.count);

    k_mutex_unlock(&route_lock);
}

const char* get_current_stop(void)
{
    const char* name = NULL;

    k_mutex_lock(&route_lock, K_FOREVER);

    if (state.count > 0 && state.current_index < state.count)
    {
        name = state.stops[state.current_index];
    }

    k_mutex_unlock(&route_lock);
    return name;
}

int get_current_stop_index(void)
{
    int index = -1;

    k_mutex_lock(&route_lock, K_FOREVER);

    if (state.count > 0 && state.current_index < state.count)
    {
        index = state.current_index;
    }

    k_mutex_unlock(&route_lock);
    return index;
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

    if (state.count > 0 && state.current_index < state.count)
    {
        state.current_index++;

        if (state.current_index < state.count)
        {
            LOG_INF("Advanced to stop %d/%d: \"%s\"", state.current_index + 1, state.count,
                    state.stops[state.current_index]);
        }
        else
        {
            LOG_INF("Route completed after %d stop(s)", state.count);
        }

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

int route_manager_parse_json(const char* json, size_t len)
{
    /*
     * cJSON_ParseWithLength requires a null-terminated string internally
     * but respects the length parameter to bound the parse.  We use it
     * instead of cJSON_Parse so we don't depend on the MQTT payload
     * being null-terminated.
     */
    cJSON* root = cJSON_ParseWithLength(json, len);

    if (root == NULL)
    {
        LOG_ERR("Route JSON parse failed");
        return -EINVAL;
    }

    if (!cJSON_IsArray(root))
    {
        LOG_ERR("Route JSON payload is not an array");
        cJSON_Delete(root);
        return -EINVAL;
    }

    int array_size = cJSON_GetArraySize(root);

    if (array_size == 0)
    {
        LOG_WRN("Received empty route array – clearing route");
        route_manager_clear();
        cJSON_Delete(root);
        return 0;
    }

    int count = array_size;

    if (count > CONFIG_DHL_ROUTE_MAX_STOPS)
    {
        LOG_WRN("Route has %d stops, clamping to %d", count, CONFIG_DHL_ROUTE_MAX_STOPS);
        count = CONFIG_DHL_ROUTE_MAX_STOPS;
    }

    /* Validate every element is a string before committing the update */
    const cJSON* elem;
    int idx = 0;

    cJSON_ArrayForEach(elem, root)
    {
        if (idx >= count)
        {
            break;
        }
        if (!cJSON_IsString(elem) || elem->valuestring == NULL)
        {
            LOG_ERR("Route element %d is not a string", idx);
            cJSON_Delete(root);
            return -EINVAL;
        }
        idx++;
    }

    /* Build a temporary pointer array for route_manager_update */
    const char* stops[CONFIG_DHL_ROUTE_MAX_STOPS];

    idx = 0;
    cJSON_ArrayForEach(elem, root)
    {
        if (idx >= count)
        {
            break;
        }
        stops[idx] = elem->valuestring;
        idx++;
    }

    route_manager_update(stops, count);
    cJSON_Delete(root);

    return 0;
}
