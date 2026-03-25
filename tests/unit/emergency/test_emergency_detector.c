#include "emergency_detector.h"
#include "unity.h"
#include <string.h>

void setUp(void)
{ /* nothing required */
}
void tearDown(void)
{ /* nothing required */
}

/* ------------------------------------------------------------------ */
/* emergency_lib_version                                                */
/* ------------------------------------------------------------------ */

void test_version_not_null(void)
{
    const char* ver = emergency_lib_version();
    TEST_ASSERT_NOT_NULL(ver);
}

void test_version_not_empty(void)
{
    const char* ver = emergency_lib_version();
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(ver));
}

/* ------------------------------------------------------------------ */
/* evaluate_emergency — probability 0%: must always return NONE        */
/* ------------------------------------------------------------------ */

void test_probability_zero_returns_none(void)
{
    emergency_config_t cfg = {.probability_percent = 0};

    /* Run many iterations to be statistically certain */
    for (int i = 0; i < 500; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        TEST_ASSERT_EQUAL_INT(EMERGENCY_CODE_NONE, r.emergency_code);
    }
}

/* ------------------------------------------------------------------ */
/* evaluate_emergency — probability 100%: must always trigger          */
/* ------------------------------------------------------------------ */

void test_probability_hundred_always_triggers(void)
{
    emergency_config_t cfg = {.probability_percent = 100};

    for (int i = 0; i < 100; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        TEST_ASSERT_NOT_EQUAL(EMERGENCY_CODE_NONE, r.emergency_code);
    }
}

void test_probability_hundred_valid_code(void)
{
    emergency_config_t cfg = {.probability_percent = 100};
    emergency_result_t r = evaluate_emergency(&cfg);

    int valid = (r.emergency_code == EMERGENCY_CODE_WEATHER || r.emergency_code == EMERGENCY_CODE_INFECTION ||
                 r.emergency_code == EMERGENCY_CODE_ENEMY_THREAT);

    TEST_ASSERT_TRUE(valid);
}

void test_probability_hundred_nonempty_type(void)
{
    emergency_config_t cfg = {.probability_percent = 100};
    emergency_result_t r = evaluate_emergency(&cfg);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(r.emergency_type));
}

void test_probability_hundred_positive_severity(void)
{
    emergency_config_t cfg = {.probability_percent = 100};
    emergency_result_t r = evaluate_emergency(&cfg);
    TEST_ASSERT_GREATER_THAN(0, r.severity);
}

/* ------------------------------------------------------------------ */
/* evaluate_emergency — correct type/severity mapping                  */
/* ------------------------------------------------------------------ */

void test_weather_severity(void)
{
    /* Force many iterations and check every WEATHER result has severity == 3 */
    emergency_config_t cfg = {.probability_percent = 100};

    for (int i = 0; i < 300; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        if (r.emergency_code == EMERGENCY_CODE_WEATHER)
        {
            TEST_ASSERT_EQUAL_STRING(EMERGENCY_TYPE_WEATHER, r.emergency_type);
            TEST_ASSERT_EQUAL_INT(SEVERITY_HIGH, r.severity);
        }
    }
}

void test_infection_severity(void)
{
    emergency_config_t cfg = {.probability_percent = 100};

    for (int i = 0; i < 300; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        if (r.emergency_code == EMERGENCY_CODE_INFECTION)
        {
            TEST_ASSERT_EQUAL_STRING(EMERGENCY_TYPE_INFECTION, r.emergency_type);
            TEST_ASSERT_EQUAL_INT(SEVERITY_CRITICAL, r.severity);
        }
    }
}

void test_enemy_threat_severity(void)
{
    emergency_config_t cfg = {.probability_percent = 100};

    for (int i = 0; i < 300; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        if (r.emergency_code == EMERGENCY_CODE_ENEMY_THREAT)
        {
            TEST_ASSERT_EQUAL_STRING(EMERGENCY_TYPE_ENEMY_THREAT, r.emergency_type);
            TEST_ASSERT_EQUAL_INT(SEVERITY_CRITICAL, r.severity);
        }
    }
}

/* ------------------------------------------------------------------ */
/* evaluate_emergency — all 3 types appear with 100% probability       */
/* ------------------------------------------------------------------ */

void test_all_three_types_appear(void)
{
    emergency_config_t cfg = {.probability_percent = 100};

    int saw_weather = 0;
    int saw_infection = 0;
    int saw_enemy_threat = 0;

    for (int i = 0; i < 1000; i++)
    {
        emergency_result_t r = evaluate_emergency(&cfg);
        if (r.emergency_code == EMERGENCY_CODE_WEATHER)
            saw_weather = 1;
        if (r.emergency_code == EMERGENCY_CODE_INFECTION)
            saw_infection = 1;
        if (r.emergency_code == EMERGENCY_CODE_ENEMY_THREAT)
            saw_enemy_threat = 1;

        if (saw_weather && saw_infection && saw_enemy_threat)
            break;
    }

    TEST_ASSERT_TRUE_MESSAGE(saw_weather, "WEATHER type never generated");
    TEST_ASSERT_TRUE_MESSAGE(saw_infection, "INFECTION type never generated");
    TEST_ASSERT_TRUE_MESSAGE(saw_enemy_threat, "ENEMY_THREAT type never generated");
}

/* ------------------------------------------------------------------ */
/* evaluate_emergency — NULL config uses default probability           */
/* ------------------------------------------------------------------ */

void test_null_config_no_crash(void)
{
    /* Should not crash and must return a valid structure */
    emergency_result_t r = evaluate_emergency(NULL);

    /* Code must be NONE or a known code */
    int valid = (r.emergency_code == EMERGENCY_CODE_NONE || r.emergency_code == EMERGENCY_CODE_WEATHER ||
                 r.emergency_code == EMERGENCY_CODE_INFECTION || r.emergency_code == EMERGENCY_CODE_ENEMY_THREAT);

    TEST_ASSERT_TRUE(valid);
}

/* ------------------------------------------------------------------ */
/* NONE result has empty type string (or at least code == NONE)        */
/* ------------------------------------------------------------------ */

void test_none_result_code_zero(void)
{
    emergency_config_t cfg = {.probability_percent = 0};
    emergency_result_t r = evaluate_emergency(&cfg);
    TEST_ASSERT_EQUAL_INT(EMERGENCY_CODE_NONE, r.emergency_code);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_version_not_null);
    RUN_TEST(test_version_not_empty);

    RUN_TEST(test_probability_zero_returns_none);

    RUN_TEST(test_probability_hundred_always_triggers);
    RUN_TEST(test_probability_hundred_valid_code);
    RUN_TEST(test_probability_hundred_nonempty_type);
    RUN_TEST(test_probability_hundred_positive_severity);

    RUN_TEST(test_weather_severity);
    RUN_TEST(test_infection_severity);
    RUN_TEST(test_enemy_threat_severity);

    RUN_TEST(test_all_three_types_appear);

    RUN_TEST(test_null_config_no_crash);
    RUN_TEST(test_none_result_code_zero);

    return UNITY_END();
}
