#ifndef ADMIN_CLI_INTERFACE_H
#define ADMIN_CLI_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#define ADMIN_RESPONSE_MAX 4096

    /* ===== Symbols the .so must export ===== */

    /** Return a version string (e.g. "1.0.0"). */
    const char* admin_cli_version(void);

    /** Initialise the plugin with a PostgreSQL connection string.
     *  The plugin creates its own DB connection internally.
     *  @return 0 on success, negative on error.  */
    int admin_cli_init(const char* conn_string);

    /** Handle one CLI command.
     *  @param raw_json   Full JSON message from CLI client.
     *  @param resp_json  Buffer for the response JSON.
     *  @param max_len    Size of resp_json.
     *  @return 0 on success, negative on error.  */
    int admin_cli_handle(const char* raw_json, char* resp_json, size_t max_len);

    /** Clean up (close DB connection). */
    void admin_cli_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ADMIN_CLI_INTERFACE_H */
