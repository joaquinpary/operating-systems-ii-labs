#ifndef API_GATEWAY_INTERFACE_H
#define API_GATEWAY_INTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#define GATEWAY_RESPONSE_MAX 4096
#define GATEWAY_SIDE_MSG_MAX 1024

    /**
     * Optional side-effect produced by the plugin.
     * When a command needs to forward a message to a client (e.g. dispatch
     * an order to a hub), the plugin fills this struct instead of forcing
     * message_handler to re-parse the response JSON.
     */
    typedef struct gateway_side_effect
    {
        int  has_message;                           /**< Non-zero when send_json is populated. */
        char target_username[64];                   /**< Username to route the message to.     */
        char send_json[GATEWAY_SIDE_MSG_MAX];       /**< Serialised message_t JSON.            */
    } gateway_side_effect_t;

    /* ===== Symbols the .so must export ===== */

    /** Return a version string (e.g. "1.0.0"). */
    const char* api_gateway_version(void);

    /** Initialise the plugin with a PostgreSQL connection string.
     *  The plugin creates its own DB connection internally.
     *  @return 0 on success, negative on error.  */
    int api_gateway_init(const char* conn_string);

    /** Handle one gateway command.
     *  @param raw_json   Full JSON message from gateway client.
     *  @param resp_json  Buffer for the response JSON (sent back to gateway).
     *  @param max_len    Size of resp_json.
     *  @param side       Optional side-effect (message to forward to a client).
     *                    The caller must zero-initialise the struct before calling.
     *  @return 0 on success, negative on error.  */
    int api_gateway_handle(const char* raw_json, char* resp_json, size_t max_len,
                           gateway_side_effect_t* side);

    /** Clean up (close DB connection). */
    void api_gateway_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* API_GATEWAY_INTERFACE_H */
