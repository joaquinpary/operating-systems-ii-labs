#ifndef TIMERS_H
#define TIMERS_H

#include <sys/timerfd.h>

typedef struct
{
    int inventory_update_interval_ms;
    int consume_stock_min_ms;
    int consume_stock_max_ms;
    int consume_min_amount;
    int consume_max_amount;
    int low_stock_threshold;
    int critical_stock_threshold;
    int max_stock_per_item;
    int emergency_check_interval_ms;
    int ack_timeout_ms;
    int max_retries;
    int response_delay_min_ms;
    int response_delay_max_ms;
} timer_config_t;

/**
 * @brief Load timer configuration from environment variables.
 *
 * Environment variables:
 * - CLIENT_INVENTORY_INTERVAL_MS
 * - CLIENT_CONSUME_MIN_MS
 * - CLIENT_CONSUME_MAX_MS
 * - CLIENT_CONSUME_MIN_AMOUNT
 * - CLIENT_CONSUME_MAX_AMOUNT
 * - CLIENT_LOW_STOCK_THRESHOLD
 * - CLIENT_CRITICAL_STOCK_THRESHOLD
 * - CLIENT_MAX_STOCK_PER_ITEM
 * - CLIENT_EMERGENCY_INTERVAL_MS
 * - CLIENT_ACK_TIMEOUT_MS
 * - CLIENT_MAX_RETRIES
 * - CLIENT_RESPONSE_DELAY_MIN_MS
 * - CLIENT_RESPONSE_DELAY_MAX_MS
 */
void load_timer_config(void);

/**
 * @brief Get pointer to loaded timer configuration.
 *
 * load_timer_config() must be called before using this accessor.
 */
const timer_config_t* get_timer_config(void);

/**
 * @brief Convert milliseconds to itimerspec.
 *
 * Result configures an one-shot timer in it_value and disabled periodic interval.
 */
struct itimerspec ms_to_itimerspec(int ms);

#endif // TIMERS_H
