#ifndef API_GATEWAY_INTERFACE_H
#define API_GATEWAY_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#define GATEWAY_RESPONSE_MAX 4096

    /* ===== Symbols the .so must export ===== */

    /** Return a version string (e.g. "1.0.0"). */
    const char* api_gateway_version(void);

    /** Initialise the plugin with a PostgreSQL connection string.
     *  The plugin creates its own DB connection internally.
     *  @return 0 on success, negative on error.  */
    int api_gateway_init(const char* conn_string);

    /** Handle one gateway command.
     *  @param raw_json   Full JSON message from gateway client.
     *  @param resp_json  Buffer for the response JSON.
     *  @param max_len    Size of resp_json.
     *  @return 0 on success, negative on error.  */
    int api_gateway_handle(const char* raw_json, char* resp_json, size_t max_len);

    /** Clean up (close DB connection). */
    void api_gateway_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* API_GATEWAY_INTERFACE_H */
