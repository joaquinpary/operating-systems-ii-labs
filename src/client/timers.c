#define _POSIX_C_SOURCE 200809L
#include "timers.h"
#include "logger.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static timer_config_t timer_config = {
    .inventory_update_interval_ms = 60000,
    .consume_stock_min_ms = 10000,
    .consume_stock_max_ms = 30000,
    .consume_min_amount = 1,
    .consume_max_amount = 20,
    .low_stock_threshold = 20,
    .critical_stock_threshold = 5,
    .max_stock_per_item = 100,
    .emergency_check_interval_ms = 30000,
    .ack_timeout_ms = 5000,
    .max_retries = 3,
    .response_delay_min_ms = 50,
    .response_delay_max_ms = 500,
};

static int parse_positive_env(const char* key, int fallback)
{
    const char* value = getenv(key);
    if (value == NULL || value[0] == '\0')
    {
        return fallback;
    }

    errno = 0;
    char* endptr = NULL;
    long parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0' || parsed <= 0 || parsed > INT_MAX)
    {
        LOG_WARNING_MSG("Invalid value for %s='%s', using default %d", key, value, fallback);
        return fallback;
    }

    return (int)parsed;
}

void load_timer_config(void)
{
    timer_config.inventory_update_interval_ms =
        parse_positive_env("CLIENT_INVENTORY_INTERVAL_MS", timer_config.inventory_update_interval_ms);
    timer_config.consume_stock_min_ms = parse_positive_env("CLIENT_CONSUME_MIN_MS", timer_config.consume_stock_min_ms);
    timer_config.consume_stock_max_ms = parse_positive_env("CLIENT_CONSUME_MAX_MS", timer_config.consume_stock_max_ms);
    timer_config.consume_min_amount = parse_positive_env("CLIENT_CONSUME_MIN_AMOUNT", timer_config.consume_min_amount);
    timer_config.consume_max_amount = parse_positive_env("CLIENT_CONSUME_MAX_AMOUNT", timer_config.consume_max_amount);
    timer_config.low_stock_threshold =
        parse_positive_env("CLIENT_LOW_STOCK_THRESHOLD", timer_config.low_stock_threshold);
    timer_config.critical_stock_threshold =
        parse_positive_env("CLIENT_CRITICAL_STOCK_THRESHOLD", timer_config.critical_stock_threshold);
    timer_config.max_stock_per_item = parse_positive_env("CLIENT_MAX_STOCK_PER_ITEM", timer_config.max_stock_per_item);
    timer_config.emergency_check_interval_ms =
        parse_positive_env("CLIENT_EMERGENCY_INTERVAL_MS", timer_config.emergency_check_interval_ms);
    timer_config.ack_timeout_ms = parse_positive_env("CLIENT_ACK_TIMEOUT_MS", timer_config.ack_timeout_ms);
    timer_config.max_retries = parse_positive_env("CLIENT_MAX_RETRIES", timer_config.max_retries);
    timer_config.response_delay_min_ms =
        parse_positive_env("CLIENT_RESPONSE_DELAY_MIN_MS", timer_config.response_delay_min_ms);
    timer_config.response_delay_max_ms =
        parse_positive_env("CLIENT_RESPONSE_DELAY_MAX_MS", timer_config.response_delay_max_ms);

    if (timer_config.consume_stock_min_ms > timer_config.consume_stock_max_ms)
    {
        LOG_WARNING_MSG("CLIENT_CONSUME_MIN_MS > CLIENT_CONSUME_MAX_MS, swapping values");
        int tmp = timer_config.consume_stock_min_ms;
        timer_config.consume_stock_min_ms = timer_config.consume_stock_max_ms;
        timer_config.consume_stock_max_ms = tmp;
    }

    if (timer_config.response_delay_min_ms > timer_config.response_delay_max_ms)
    {
        LOG_WARNING_MSG("CLIENT_RESPONSE_DELAY_MIN_MS > CLIENT_RESPONSE_DELAY_MAX_MS, swapping values");
        int tmp = timer_config.response_delay_min_ms;
        timer_config.response_delay_min_ms = timer_config.response_delay_max_ms;
        timer_config.response_delay_max_ms = tmp;
    }

    if (timer_config.consume_min_amount > timer_config.consume_max_amount)
    {
        LOG_WARNING_MSG("CLIENT_CONSUME_MIN_AMOUNT > CLIENT_CONSUME_MAX_AMOUNT, swapping values");
        int tmp = timer_config.consume_min_amount;
        timer_config.consume_min_amount = timer_config.consume_max_amount;
        timer_config.consume_max_amount = tmp;
    }

    LOG_INFO_MSG(
        "Timer config loaded: inv=%dms consume=[%d,%d]ms consume_amount=[%d,%d] emergency=%dms ack=%dms retries=%d "
        "response_delay=[%d,%d]ms",
        timer_config.inventory_update_interval_ms, timer_config.consume_stock_min_ms, timer_config.consume_stock_max_ms,
        timer_config.consume_min_amount, timer_config.consume_max_amount, timer_config.emergency_check_interval_ms,
        timer_config.ack_timeout_ms, timer_config.max_retries, timer_config.response_delay_min_ms,
        timer_config.response_delay_max_ms);
}

const timer_config_t* get_timer_config(void)
{
    return &timer_config;
}

struct itimerspec ms_to_itimerspec(int ms)
{
    struct itimerspec spec;
    spec.it_interval.tv_sec = 0;
    spec.it_interval.tv_nsec = 0;
    spec.it_value.tv_sec = ms / 1000;
    spec.it_value.tv_nsec = (long)(ms % 1000) * 1000000L;
    return spec;
}