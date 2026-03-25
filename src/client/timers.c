#define _POSIX_C_SOURCE 200809L
#include "timers.h"
#include "logger.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#define TIMER_INVENTORY_INTERVAL_MS 60000
#define TIMER_CONSUME_MIN_MS 10000
#define TIMER_CONSUME_MAX_MS 30000
#define TIMER_CONSUME_MIN_AMOUNT 1
#define TIMER_CONSUME_MAX_AMOUNT 20
#define TIMER_LOW_STOCK_THRESHOLD 20
#define TIMER_CRITICAL_STOCK_THRESHOLD 5
#define TIMER_MAX_STOCK_PER_ITEM 100
#define TIMER_EMERGENCY_CHECK_INTERVAL_MS 30000
#define TIMER_ACK_CHECK_INTERVAL_SEC 1
#define TIMER_RECV_TIMEOUT_SEC 1
#define TIMER_LOOP_TIMEOUT_SEC 1
#define TIMER_ACK_TIMEOUT_MS 5000
#define TIMER_MAX_RETRIES 3
#define TIMER_RESPONSE_DELAY_MIN_MS 50
#define TIMER_RESPONSE_DELAY_MAX_MS 500

static timer_config_t timer_config = {
    .inventory_update_interval_ms = TIMER_INVENTORY_INTERVAL_MS,
    .consume_stock_min_ms = TIMER_CONSUME_MIN_MS,
    .consume_stock_max_ms = TIMER_CONSUME_MAX_MS,
    .consume_min_amount = TIMER_CONSUME_MIN_AMOUNT,
    .consume_max_amount = TIMER_CONSUME_MAX_AMOUNT,
    .low_stock_threshold = TIMER_LOW_STOCK_THRESHOLD,
    .critical_stock_threshold = TIMER_CRITICAL_STOCK_THRESHOLD,
    .max_stock_per_item = TIMER_MAX_STOCK_PER_ITEM,
    .emergency_check_interval_ms = TIMER_EMERGENCY_CHECK_INTERVAL_MS,
    .ack_check_interval_sec = TIMER_ACK_CHECK_INTERVAL_SEC,
    .recv_timeout_sec = TIMER_RECV_TIMEOUT_SEC,
    .loop_timeout_sec = TIMER_LOOP_TIMEOUT_SEC,
    .ack_timeout_ms = TIMER_ACK_TIMEOUT_MS,
    .max_retries = TIMER_MAX_RETRIES,
    .response_delay_min_ms = TIMER_RESPONSE_DELAY_MIN_MS,
    .response_delay_max_ms = TIMER_RESPONSE_DELAY_MAX_MS,
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
    timer_config.ack_check_interval_sec =
        parse_positive_env("CLIENT_ACK_CHECK_INTERVAL_SEC", timer_config.ack_check_interval_sec);
    timer_config.recv_timeout_sec = parse_positive_env("CLIENT_RECV_TIMEOUT_SEC", timer_config.recv_timeout_sec);
    timer_config.loop_timeout_sec = parse_positive_env("CLIENT_LOOP_TIMEOUT_SEC", timer_config.loop_timeout_sec);
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

    LOG_INFO_MSG("Timer config loaded: inv=%dms consume=[%d,%d]ms consume_amount=[%d,%d] emergency=%dms ack_check=%ds "
                 "recv_timeout=%ds loop_timeout=%ds ack=%dms retries=%d response_delay=[%d,%d]ms",
                 timer_config.inventory_update_interval_ms, timer_config.consume_stock_min_ms,
                 timer_config.consume_stock_max_ms, timer_config.consume_min_amount, timer_config.consume_max_amount,
                 timer_config.emergency_check_interval_ms, timer_config.ack_check_interval_sec,
                 timer_config.recv_timeout_sec, timer_config.loop_timeout_sec, timer_config.ack_timeout_ms,
                 timer_config.max_retries, timer_config.response_delay_min_ms, timer_config.response_delay_max_ms);
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
