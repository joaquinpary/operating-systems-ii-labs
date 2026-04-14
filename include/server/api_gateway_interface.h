#ifndef API_GATEWAY_INTERFACE_H
#define API_GATEWAY_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#define GATEWAY_RESPONSE_MAX 4096
#define GATEWAY_SIDE_MSG_MAX 1024

    typedef struct gateway_side_effect
    {
        int has_message;
        char target_username[64];
        char send_json[GATEWAY_SIDE_MSG_MAX];
    } gateway_side_effect_t;

    /** Return a version string (e.g. "1.0.0"). */
    const char* api_gateway_version(void);

    /** Initialise the plugin with a PostgreSQL connection string.
     *  The plugin creates its own DB connection internally.
     *  @param conn_string PostgreSQL connection string used by the plugin.
     *  @return 0 on success, negative on error.  */
    int api_gateway_init(const char* conn_string);

    /** Handle one gateway command.
     *  @param raw_json   Full JSON message from gateway client.
     *  @param resp_json  Buffer for the response JSON (sent back to gateway).
     *  @param max_len    Size of resp_json.
     *  @param side       Optional side-effect (message to forward to a client).
     *                    The caller must zero-initialise the struct before calling.
     *  @return 0 on success, negative on error.  */
    int api_gateway_handle(const char* raw_json, char* resp_json, size_t max_len, gateway_side_effect_t* side);

    /** Clean up (close DB connection). */
    void api_gateway_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
