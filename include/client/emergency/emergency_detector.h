#ifndef EMERGENCY_DETECTOR_H
#define EMERGENCY_DETECTOR_H

// ==================== EMERGENCY CODES (FR005) ====================

#define EMERGENCY_CODE_NONE 0
#define EMERGENCY_CODE_WEATHER 2001      // Extreme weather conditions
#define EMERGENCY_CODE_INFECTION 2002    // Contamination/infection in the logistics chain
#define EMERGENCY_CODE_ENEMY_THREAT 2003 // External threat (theft, sabotage)

// Emergency type strings (carried in the message payload)
#define EMERGENCY_TYPE_WEATHER "WEATHER"
#define EMERGENCY_TYPE_INFECTION "INFECTION"
#define EMERGENCY_TYPE_ENEMY_THREAT "ENEMY_THREAT"

// Severity levels
#define SEVERITY_NONE 0
#define SEVERITY_LOW 1
#define SEVERITY_MEDIUM 2
#define SEVERITY_HIGH 3
#define SEVERITY_CRITICAL 4

// Default trigger probability (2%)
#define DEFAULT_EMERGENCY_PROBABILITY 2
#define EMERGENCY_TYPE_COUNT 3

#define EMERGENCY_TYPE_SIZE 20

// ==================== STRUCTURES ====================

// Result of a single evaluation
typedef struct
{
    int emergency_code;                       // 0 = no emergency, 2001/2002/2003
    char emergency_type[EMERGENCY_TYPE_SIZE]; // "WEATHER", "INFECTION", "ENEMY_THREAT"
    int severity;                             // 0-4
} emergency_result_t;

// Configuration (pass NULL to use defaults)
typedef struct
{
    int probability_percent; // Probability of triggering an emergency per evaluation (default: 2)
} emergency_config_t;

// ==================== PUBLIC API ====================

/**
 * @brief Returns the library version string (used to verify compatibility at load time).
 * @return Version string in "MAJOR.MINOR.PATCH" format.
 */
const char* emergency_lib_version(void);

/**
 * @brief Evaluates whether an emergency occurs based on random probability.
 *
 * Each call has a configurable probability (~2% by default) of triggering an
 * emergency. When triggered, one of the three FR005 types is chosen uniformly
 * at random: WEATHER, INFECTION, or ENEMY_THREAT.
 *
 * Trigger probability  : probability_percent / 100
 * Type distribution    : uniform, 1/3 each when triggered.
 *
 * @param config Probability configuration. NULL uses DEFAULT_EMERGENCY_PROBABILITY (2%).
 * @return emergency_result_t with:
 *   - code=0, type="", severity=0  → no emergency
 *   - code=2001/2002/2003          → emergency triggered with its type and severity
 */
emergency_result_t evaluate_emergency(const emergency_config_t* config);

#endif // EMERGENCY_DETECTOR_H
