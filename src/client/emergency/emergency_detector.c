#include "emergency_detector.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LIB_VERSION "1.0.0"

static int seeded = 0;

/**
 * @brief Seed the pseudo-random generator once per process lifetime.
 */
static void ensure_seeded(void)
{
    if (!seeded)
    {
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        srand(seed);
        seeded = 1;
    }
}

const char* emergency_lib_version(void)
{
    return LIB_VERSION;
}

emergency_result_t evaluate_emergency(const emergency_config_t* config)
{
    emergency_result_t result;
    memset(&result, 0, sizeof(result));

    ensure_seeded();

    int probability = (config != NULL) ? config->probability_percent : DEFAULT_EMERGENCY_PROBABILITY;

    if (probability < 0)
        probability = 0;
    if (probability > 100)
        probability = 100;

    int roll = rand() % 1000;
    if (roll >= probability)
    {
        return result;
    }

    int type_roll = rand() % EMERGENCY_TYPE_COUNT;

    switch (type_roll)
    {
    case 0:
        result.emergency_code = EMERGENCY_CODE_WEATHER;
        strncpy(result.emergency_type, EMERGENCY_TYPE_WEATHER, sizeof(result.emergency_type) - 1);
        result.severity = SEVERITY_HIGH;
        break;
    case 1:
        result.emergency_code = EMERGENCY_CODE_INFECTION;
        strncpy(result.emergency_type, EMERGENCY_TYPE_INFECTION, sizeof(result.emergency_type) - 1);
        result.severity = SEVERITY_CRITICAL;
        break;
    case 2:
    default:
        result.emergency_code = EMERGENCY_CODE_ENEMY_THREAT;
        strncpy(result.emergency_type, EMERGENCY_TYPE_ENEMY_THREAT, sizeof(result.emergency_type) - 1);
        result.severity = SEVERITY_CRITICAL;
        break;
    }

    result.emergency_type[sizeof(result.emergency_type) - 1] = '\0';
    return result;
}
