#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdbool.h>

#define PROV_SSID_MAX_LEN   33
#define PROV_PSK_MAX_LEN    65
#define PROV_USER_MAX_LEN   33

/**
 * @brief Credential set loaded from / saved to NVS flash.
 */
struct prov_config {
	char ssid[PROV_SSID_MAX_LEN];
	char psk[PROV_PSK_MAX_LEN];
	char employee_id[PROV_USER_MAX_LEN];
};

/**
 * @brief Load provisioned credentials from NVS.
 *
 * @param cfg Output struct.
 * @return 0 if valid credentials were found, negative errno otherwise.
 */
int prov_load(struct prov_config *cfg);

/**
 * @brief Save credentials to NVS.
 *
 * @param cfg Credentials to persist.
 * @return 0 on success, negative errno otherwise.
 */
int prov_save(const struct prov_config *cfg);

/**
 * @brief Erase stored credentials (force re-provisioning on next boot).
 */
void prov_erase(void);

#endif /* PROVISIONING_H */
