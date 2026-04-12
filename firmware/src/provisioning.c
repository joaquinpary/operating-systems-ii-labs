#include "provisioning.h"

#include <string.h>
#include <errno.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(provisioning, LOG_LEVEL_INF);

/* ── NVS / settings keys ─────────────────────────────────────────────────── */
#define PROV_SETTINGS_ROOT  "prov"
#define KEY_SSID            PROV_SETTINGS_ROOT "/ssid"
#define KEY_PSK             PROV_SETTINGS_ROOT "/psk"
#define KEY_EMPLOYEE_ID     PROV_SETTINGS_ROOT "/eid"

/* ══════════════════════════════════════════════════════════════════════════
 * Settings handler – populated when settings_load() runs
 * ══════════════════════════════════════════════════════════════════════════ */

static struct prov_config g_flash_cfg;

static int prov_settings_set(const char *key, size_t len,
			     settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, "ssid") == 0) {
		if (len > sizeof(g_flash_cfg.ssid) - 1) {
			len = sizeof(g_flash_cfg.ssid) - 1;
		}
		if (read_cb(cb_arg, g_flash_cfg.ssid, len) < 0) {
			return -EIO;
		}
		g_flash_cfg.ssid[len] = '\0';
	} else if (strcmp(key, "psk") == 0) {
		if (len > sizeof(g_flash_cfg.psk) - 1) {
			len = sizeof(g_flash_cfg.psk) - 1;
		}
		if (read_cb(cb_arg, g_flash_cfg.psk, len) < 0) {
			return -EIO;
		}
		g_flash_cfg.psk[len] = '\0';
	} else if (strcmp(key, "eid") == 0) {
		if (len > sizeof(g_flash_cfg.employee_id) - 1) {
			len = sizeof(g_flash_cfg.employee_id) - 1;
		}
		if (read_cb(cb_arg, g_flash_cfg.employee_id, len) < 0) {
			return -EIO;
		}
		g_flash_cfg.employee_id[len] = '\0';
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prov, PROV_SETTINGS_ROOT, NULL,
				prov_settings_set, NULL, NULL);

/* ══════════════════════════════════════════════════════════════════════════
 * NVS helpers (via Zephyr settings subsystem)
 * ══════════════════════════════════════════════════════════════════════════ */

int prov_load(struct prov_config *cfg)
{
	/* g_flash_cfg was filled by the settings handler during settings_load() */
	if (g_flash_cfg.ssid[0] != '\0' && g_flash_cfg.employee_id[0] != '\0') {
		*cfg = g_flash_cfg;
		LOG_INF("Loaded provisioning from NVS: ssid=%s eid=%s",
			cfg->ssid, cfg->employee_id);
		return 0;
	}

	/* Fall back to compile-time credentials if set */
	if (sizeof(CONFIG_DHL_WIFI_SSID) > 1) {
		strncpy(cfg->ssid, CONFIG_DHL_WIFI_SSID, sizeof(cfg->ssid) - 1);
		strncpy(cfg->psk,  CONFIG_DHL_WIFI_PSK,  sizeof(cfg->psk) - 1);
		strncpy(cfg->employee_id, CONFIG_DHL_EMPLOYEE_ID,
			sizeof(cfg->employee_id) - 1);
		cfg->ssid[sizeof(cfg->ssid) - 1] = '\0';
		cfg->psk[sizeof(cfg->psk) - 1]   = '\0';
		cfg->employee_id[sizeof(cfg->employee_id) - 1] = '\0';
		LOG_INF("Using compile-time credentials: ssid=%s", cfg->ssid);
		return 0;
	}

	return -ENOENT;
}

int prov_save(const struct prov_config *cfg)
{
	int rc;

	rc = settings_save_one(KEY_SSID, cfg->ssid, strlen(cfg->ssid) + 1);
	if (rc) {
		return rc;
	}
	rc = settings_save_one(KEY_PSK, cfg->psk, strlen(cfg->psk) + 1);
	if (rc) {
		return rc;
	}
	rc = settings_save_one(KEY_EMPLOYEE_ID, cfg->employee_id,
			       strlen(cfg->employee_id) + 1);
	if (rc) {
		return rc;
	}

	/* Also update in-memory cache */
	g_flash_cfg = *cfg;

	LOG_INF("Provisioning saved to flash");
	return 0;
}

void prov_erase(void)
{
	settings_delete(KEY_SSID);
	settings_delete(KEY_PSK);
	settings_delete(KEY_EMPLOYEE_ID);
	memset(&g_flash_cfg, 0, sizeof(g_flash_cfg));
	LOG_INF("Provisioning erased");
}

