#define _POSIX_C_SOURCE 200809L
#include "logger.h"
#include "timers.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

#define TEST_LOG_FILE "/tmp/test_timers.log"
#define TEST_LOG_FILE_SIZE (10 * 1024 * 1024)
#define TEST_LOG_BACKUPS 3

// Hardcoded defaults from timers.c
#define DEFAULT_INVENTORY_INTERVAL_MS 60000
#define DEFAULT_CONSUME_MIN_MS 10000
#define DEFAULT_CONSUME_MAX_MS 30000
#define DEFAULT_CONSUME_MIN_AMOUNT 1
#define DEFAULT_CONSUME_MAX_AMOUNT 20
#define DEFAULT_LOW_STOCK_THRESHOLD 20
#define DEFAULT_CRITICAL_THRESHOLD 5
#define DEFAULT_MAX_STOCK 100
#define DEFAULT_EMERGENCY_INTERVAL_MS 30000
#define DEFAULT_ACK_TIMEOUT_MS 5000
#define DEFAULT_MAX_RETRIES 3
#define DEFAULT_RESPONSE_DELAY_MIN_MS 50
#define DEFAULT_RESPONSE_DELAY_MAX_MS 500

static void clear_all_timer_env(void)
{
    unsetenv("CLIENT_INVENTORY_INTERVAL_MS");
    unsetenv("CLIENT_CONSUME_MIN_MS");
    unsetenv("CLIENT_CONSUME_MAX_MS");
    unsetenv("CLIENT_CONSUME_MIN_AMOUNT");
    unsetenv("CLIENT_CONSUME_MAX_AMOUNT");
    unsetenv("CLIENT_LOW_STOCK_THRESHOLD");
    unsetenv("CLIENT_CRITICAL_STOCK_THRESHOLD");
    unsetenv("CLIENT_MAX_STOCK_PER_ITEM");
    unsetenv("CLIENT_EMERGENCY_INTERVAL_MS");
    unsetenv("CLIENT_ACK_TIMEOUT_MS");
    unsetenv("CLIENT_MAX_RETRIES");
    unsetenv("CLIENT_RESPONSE_DELAY_MIN_MS");
    unsetenv("CLIENT_RESPONSE_DELAY_MAX_MS");
}

// Reset the static timer_config to its default values before each test,
// by loading with all env vars set to the known defaults, then unsetting them.
static void reset_timer_config_to_defaults(void)
{
    setenv("CLIENT_INVENTORY_INTERVAL_MS", "60000", 1);
    setenv("CLIENT_CONSUME_MIN_MS", "10000", 1);
    setenv("CLIENT_CONSUME_MAX_MS", "30000", 1);
    setenv("CLIENT_CONSUME_MIN_AMOUNT", "1", 1);
    setenv("CLIENT_CONSUME_MAX_AMOUNT", "20", 1);
    setenv("CLIENT_LOW_STOCK_THRESHOLD", "20", 1);
    setenv("CLIENT_CRITICAL_STOCK_THRESHOLD", "5", 1);
    setenv("CLIENT_MAX_STOCK_PER_ITEM", "100", 1);
    setenv("CLIENT_EMERGENCY_INTERVAL_MS", "30000", 1);
    setenv("CLIENT_ACK_TIMEOUT_MS", "5000", 1);
    setenv("CLIENT_MAX_RETRIES", "3", 1);
    setenv("CLIENT_RESPONSE_DELAY_MIN_MS", "50", 1);
    setenv("CLIENT_RESPONSE_DELAY_MAX_MS", "500", 1);
    load_timer_config();
    clear_all_timer_env();
}

void setUp(void)
{
    logger_config_t config = {.log_file_path = TEST_LOG_FILE,
                              .max_file_size = TEST_LOG_FILE_SIZE,
                              .max_backup_files = TEST_LOG_BACKUPS,
                              .min_level = LOG_DEBUG};
    log_init(&config);
    reset_timer_config_to_defaults();
}

void tearDown(void)
{
    clear_all_timer_env();
    log_close();
}

// ==================== load_timer_config ====================

void test_load_timer_config_defaults(void)
{
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(DEFAULT_INVENTORY_INTERVAL_MS, cfg->inventory_update_interval_ms);
    TEST_ASSERT_EQUAL(DEFAULT_CONSUME_MIN_MS, cfg->consume_stock_min_ms);
    TEST_ASSERT_EQUAL(DEFAULT_CONSUME_MAX_MS, cfg->consume_stock_max_ms);
    TEST_ASSERT_EQUAL(DEFAULT_CONSUME_MIN_AMOUNT, cfg->consume_min_amount);
    TEST_ASSERT_EQUAL(DEFAULT_CONSUME_MAX_AMOUNT, cfg->consume_max_amount);
    TEST_ASSERT_EQUAL(DEFAULT_LOW_STOCK_THRESHOLD, cfg->low_stock_threshold);
    TEST_ASSERT_EQUAL(DEFAULT_CRITICAL_THRESHOLD, cfg->critical_stock_threshold);
    TEST_ASSERT_EQUAL(DEFAULT_MAX_STOCK, cfg->max_stock_per_item);
    TEST_ASSERT_EQUAL(DEFAULT_EMERGENCY_INTERVAL_MS, cfg->emergency_check_interval_ms);
    TEST_ASSERT_EQUAL(DEFAULT_ACK_TIMEOUT_MS, cfg->ack_timeout_ms);
    TEST_ASSERT_EQUAL(DEFAULT_MAX_RETRIES, cfg->max_retries);
    TEST_ASSERT_EQUAL(DEFAULT_RESPONSE_DELAY_MIN_MS, cfg->response_delay_min_ms);
    TEST_ASSERT_EQUAL(DEFAULT_RESPONSE_DELAY_MAX_MS, cfg->response_delay_max_ms);
}

void test_load_timer_config_override_ack_timeout(void)
{
    setenv("CLIENT_ACK_TIMEOUT_MS", "8000", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(8000, cfg->ack_timeout_ms);
    TEST_ASSERT_EQUAL(DEFAULT_MAX_RETRIES, cfg->max_retries);
    TEST_ASSERT_EQUAL(DEFAULT_INVENTORY_INTERVAL_MS, cfg->inventory_update_interval_ms);
}

void test_load_timer_config_override_multiple_fields(void)
{
    setenv("CLIENT_MAX_RETRIES", "5", 1);
    setenv("CLIENT_INVENTORY_INTERVAL_MS", "120000", 1);
    setenv("CLIENT_EMERGENCY_INTERVAL_MS", "15000", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(5, cfg->max_retries);
    TEST_ASSERT_EQUAL(120000, cfg->inventory_update_interval_ms);
    TEST_ASSERT_EQUAL(15000, cfg->emergency_check_interval_ms);
    TEST_ASSERT_EQUAL(DEFAULT_ACK_TIMEOUT_MS, cfg->ack_timeout_ms);
}

void test_load_timer_config_invalid_string_uses_default(void)
{
    setenv("CLIENT_ACK_TIMEOUT_MS", "not_a_number", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(DEFAULT_ACK_TIMEOUT_MS, cfg->ack_timeout_ms);
}

void test_load_timer_config_zero_value_uses_default(void)
{
    setenv("CLIENT_MAX_RETRIES", "0", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(DEFAULT_MAX_RETRIES, cfg->max_retries);
}

void test_load_timer_config_negative_value_uses_default(void)
{
    setenv("CLIENT_INVENTORY_INTERVAL_MS", "-1000", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(DEFAULT_INVENTORY_INTERVAL_MS, cfg->inventory_update_interval_ms);
}

void test_load_timer_config_empty_string_uses_default(void)
{
    setenv("CLIENT_ACK_TIMEOUT_MS", "", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(DEFAULT_ACK_TIMEOUT_MS, cfg->ack_timeout_ms);
}

void test_load_timer_config_swaps_consume_min_max(void)
{
    // min > max — should be swapped so min < max
    setenv("CLIENT_CONSUME_MIN_MS", "30000", 1);
    setenv("CLIENT_CONSUME_MAX_MS", "10000", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(10000, cfg->consume_stock_min_ms);
    TEST_ASSERT_EQUAL(30000, cfg->consume_stock_max_ms);
    TEST_ASSERT_TRUE(cfg->consume_stock_min_ms <= cfg->consume_stock_max_ms);
}

void test_load_timer_config_swaps_response_delay(void)
{
    setenv("CLIENT_RESPONSE_DELAY_MIN_MS", "500", 1);
    setenv("CLIENT_RESPONSE_DELAY_MAX_MS", "50", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(50, cfg->response_delay_min_ms);
    TEST_ASSERT_EQUAL(500, cfg->response_delay_max_ms);
    TEST_ASSERT_TRUE(cfg->response_delay_min_ms <= cfg->response_delay_max_ms);
}

void test_load_timer_config_swaps_consume_amount(void)
{
    setenv("CLIENT_CONSUME_MIN_AMOUNT", "20", 1);
    setenv("CLIENT_CONSUME_MAX_AMOUNT", "1", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(1, cfg->consume_min_amount);
    TEST_ASSERT_EQUAL(20, cfg->consume_max_amount);
    TEST_ASSERT_TRUE(cfg->consume_min_amount <= cfg->consume_max_amount);
}

void test_load_timer_config_equal_min_max_not_swapped(void)
{
    setenv("CLIENT_CONSUME_MIN_MS", "15000", 1);
    setenv("CLIENT_CONSUME_MAX_MS", "15000", 1);
    load_timer_config();
    const timer_config_t* cfg = get_timer_config();

    TEST_ASSERT_EQUAL(15000, cfg->consume_stock_min_ms);
    TEST_ASSERT_EQUAL(15000, cfg->consume_stock_max_ms);
}

// ==================== get_timer_config ====================

void test_get_timer_config_not_null(void)
{
    load_timer_config();
    TEST_ASSERT_NOT_NULL(get_timer_config());
}

void test_get_timer_config_returns_same_pointer(void)
{
    load_timer_config();
    const timer_config_t* c1 = get_timer_config();
    const timer_config_t* c2 = get_timer_config();

    TEST_ASSERT_EQUAL_PTR(c1, c2);
}

// ==================== ms_to_itimerspec ====================

void test_ms_to_itimerspec_zero(void)
{
    struct itimerspec spec = ms_to_itimerspec(0);

    TEST_ASSERT_EQUAL(0, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(0, spec.it_value.tv_nsec);
    TEST_ASSERT_EQUAL(0, spec.it_interval.tv_sec);
    TEST_ASSERT_EQUAL(0, spec.it_interval.tv_nsec);
}

void test_ms_to_itimerspec_exact_seconds(void)
{
    struct itimerspec spec = ms_to_itimerspec(1000);

    TEST_ASSERT_EQUAL(1, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(0, spec.it_value.tv_nsec);
}

void test_ms_to_itimerspec_mixed_sec_and_ms(void)
{
    struct itimerspec spec = ms_to_itimerspec(1500);

    TEST_ASSERT_EQUAL(1, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(500000000L, spec.it_value.tv_nsec);
}

void test_ms_to_itimerspec_only_ms(void)
{
    struct itimerspec spec = ms_to_itimerspec(500);

    TEST_ASSERT_EQUAL(0, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(500000000L, spec.it_value.tv_nsec);
}

void test_ms_to_itimerspec_interval_always_zero(void)
{
    struct itimerspec spec = ms_to_itimerspec(5000);

    TEST_ASSERT_EQUAL(0, spec.it_interval.tv_sec);
    TEST_ASSERT_EQUAL(0, spec.it_interval.tv_nsec);
}

void test_ms_to_itimerspec_large_value(void)
{
    // 60 000 ms = 60 s = default inventory interval
    struct itimerspec spec = ms_to_itimerspec(60000);

    TEST_ASSERT_EQUAL(60, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(0, spec.it_value.tv_nsec);
}

void test_ms_to_itimerspec_one_ms(void)
{
    struct itimerspec spec = ms_to_itimerspec(1);

    TEST_ASSERT_EQUAL(0, spec.it_value.tv_sec);
    TEST_ASSERT_EQUAL(1000000L, spec.it_value.tv_nsec);
}

// ==================== MAIN ====================

int main(void)
{
    UNITY_BEGIN();

    // load_timer_config
    RUN_TEST(test_load_timer_config_defaults);
    RUN_TEST(test_load_timer_config_override_ack_timeout);
    RUN_TEST(test_load_timer_config_override_multiple_fields);
    RUN_TEST(test_load_timer_config_invalid_string_uses_default);
    RUN_TEST(test_load_timer_config_zero_value_uses_default);
    RUN_TEST(test_load_timer_config_negative_value_uses_default);
    RUN_TEST(test_load_timer_config_empty_string_uses_default);
    RUN_TEST(test_load_timer_config_swaps_consume_min_max);
    RUN_TEST(test_load_timer_config_swaps_response_delay);
    RUN_TEST(test_load_timer_config_swaps_consume_amount);
    RUN_TEST(test_load_timer_config_equal_min_max_not_swapped);

    // get_timer_config
    RUN_TEST(test_get_timer_config_not_null);
    RUN_TEST(test_get_timer_config_returns_same_pointer);

    // ms_to_itimerspec
    RUN_TEST(test_ms_to_itimerspec_zero);
    RUN_TEST(test_ms_to_itimerspec_exact_seconds);
    RUN_TEST(test_ms_to_itimerspec_mixed_sec_and_ms);
    RUN_TEST(test_ms_to_itimerspec_only_ms);
    RUN_TEST(test_ms_to_itimerspec_interval_always_zero);
    RUN_TEST(test_ms_to_itimerspec_large_value);
    RUN_TEST(test_ms_to_itimerspec_one_ms);

    return UNITY_END();
}
