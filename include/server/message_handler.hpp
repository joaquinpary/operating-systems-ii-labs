#ifndef MESSAGE_HANDLER_HPP
#define MESSAGE_HANDLER_HPP

#include "auth_module.hpp"
#include "api_gateway_interface.h"
#include "inventory_manager.hpp"
#include "ipc.hpp"
#include <common/json_manager.h>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <vector>

enum class message_category
{
    AUTH_REQUEST,    ///< Authentication request from a client.
    ACK_MESSAGE,     ///< ACK emitted by a client for a previous outbound message.
    KEEPALIVE_MSG,   ///< Keepalive ping from an authenticated client.
    INV_UPDATE,      ///< Inventory update emitted by a hub or warehouse.
    STOCK_REQ,       ///< Stock request emitted by a hub.
    RECEIPT_CONFIRM, ///< Receipt confirmation emitted after stock delivery.
    DISPATCH_NOTICE, ///< Shipment notice emitted by a warehouse.
    REPLENISH_REQ,   ///< Replenish request emitted by a warehouse.
    EMERGENCY_ALERT, ///< Emergency alert from a hub or warehouse.
    CLI_COMMAND,     ///< Admin CLI command (handled by libadmin_cli.so).
    GATEWAY_COMMAND, ///< API Gateway command (handled by libapi_gateway.so).
    OTHER            ///< Any unrecognized or unsupported message type.
};

/**
 * message_handler — runs inside the worker process.
 *
 * It receives a request_slot_t (from the shared memory ring buffer),
 * processes the message using auth_module / inventory_manager,
 * and returns a vector of response_slot_t to be pushed back to the reactor.
 *
 * It does NOT touch session_manager or timer_manager directly (those live in the reactor).
 * Instead it emits response commands: SEND, START_ACK_TIMER, CANCEL_ACK_TIMER,
 * CLEAR_TIMERS, BLACKLIST, MARK_AUTHENTICATED.
 */
class message_handler
{
  public:
    message_handler(auth_module& auth, inventory_manager& inv_mgr, std::uint32_t ack_timeout_seconds,
                    std::uint32_t max_retries, std::uint32_t keepalive_timeout_seconds,
                    const std::string& db_conn_string = "");
    ~message_handler();

    /**
     * Generate an immediate ACK for the request if applicable.
     * This is a lightweight operation (JSON parse + ACK build, no DB access)
     * and should be called and sent BEFORE process_request to avoid ACK timeouts.
     * @return The ACK response slot, or std::nullopt if no ACK is needed.
     */
    std::optional<response_slot_t> generate_ack(const request_slot_t& request);

    /**
     * Process a request slot and produce responses (heavy business logic).
     * ACK is NOT included — call generate_ack() separately before this.
     * @return vector of response commands to post back to the reactor via shared_queue.
     */
    std::vector<response_slot_t> process_request(const request_slot_t& request);

  private:
    // Generate ACK response slot if the message requires it
    bool generate_ack_if_needed(const message_t& msg, const request_slot_t& req, response_slot_t& ack_out);

    // Handle authentication request
    std::vector<response_slot_t> handle_auth_request(const message_t& msg, const request_slot_t& req);

    // Handle ACK messages from clients
    std::vector<response_slot_t> handle_ack_message(const message_t& msg, const request_slot_t& req);

    // Handle other message types
    std::vector<response_slot_t> handle_other_message(const message_t& msg, const request_slot_t& req,
                                                      message_category category);

    // Categorize message type for routing
    message_category categorize_message(const char* msg_type) const;

    // Get response message type based on client type
    const char* get_auth_response_type(const std::string& client_type) const;

    // Helper to make response slots
    response_slot_t make_send_response(const char* session_id, const message_t& msg);
    response_slot_t make_send_to_username(const char* username, const message_t& msg);
    response_slot_t make_start_timer(const char* session_id, const message_t& msg);
    response_slot_t make_cancel_timer(const char* session_id, const char* timestamp);
    response_slot_t make_clear_timers(const char* session_id);
    response_slot_t make_blacklist(const char* session_id);
    response_slot_t make_mark_authenticated(const char* session_id, const char* client_type, const char* username);
    response_slot_t make_start_keepalive_timer(const char* session_id);
    response_slot_t make_reset_keepalive_timer(const char* session_id);

    auth_module& m_auth_module;
    inventory_manager& m_inventory_manager;
    std::uint32_t m_ack_timeout_seconds;
    std::uint32_t m_max_retries;
    std::uint32_t m_keepalive_timeout_seconds;

    // Dead-session tracking: sessions that have been disconnected.
    // Worker threads check this before doing heavy DB work.
    mutable std::shared_mutex m_dead_sessions_mutex;
    std::unordered_set<std::string> m_dead_sessions;

    // Admin CLI plugin (libadmin_cli.so) — loaded via dlopen.
    void* m_admin_lib = nullptr;
    using admin_handle_fn = int (*)(const char*, char*, size_t);
    using admin_shutdown_fn = void (*)();
    admin_handle_fn m_admin_handle = nullptr;
    admin_shutdown_fn m_admin_shutdown = nullptr;

    // API Gateway plugin (libapi_gateway.so) — loaded via dlopen.
    void* m_gateway_lib = nullptr;
    using gateway_handle_fn = int (*)(const char*, char*, size_t, gateway_side_effect_t*);
    using gateway_shutdown_fn = void (*)();
    gateway_handle_fn m_gateway_handle = nullptr;
    gateway_shutdown_fn m_gateway_shutdown = nullptr;
};

#endif // MESSAGE_HANDLER_HPP
