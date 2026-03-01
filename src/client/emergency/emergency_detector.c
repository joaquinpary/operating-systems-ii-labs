#include "emergency_detector.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LIB_VERSION "1.0.0"

// Seeded once per process lifetime
static int seeded = 0;

static void ensure_seeded(void)
{
    if (!seeded)
    {
        // XOR time(NULL) with getpid() so that each of the 1000 processes in the
        // container gets a distinct random sequence even when they start almost
        // simultaneously.
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

    // Clamp to avoid out-of-range values
    if (probability < 0)
        probability = 0;
    if (probability > 100)
        probability = 100;

    // ~(100 - probability)% of the time there is no emergency
    int roll = rand() % 100;
    if (roll >= probability)
    {
        // No emergency (~98% of the time with probability=2)
        return result;
    }

    // Emergency triggered: choose type uniformly at random
    int type_roll = rand() % 3;

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
