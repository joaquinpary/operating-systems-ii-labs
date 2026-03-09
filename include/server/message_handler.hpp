#ifndef MESSAGE_HANDLER_HPP
#define MESSAGE_HANDLER_HPP

#include "auth_module.hpp"
#include "inventory_manager.hpp"
#include "ipc.hpp"
#include <common/json_manager.h>
#include <cstdint>
#include <string>
#include <vector>

enum class message_category
{
    AUTH_REQUEST,
    ACK_MESSAGE,
    INV_UPDATE,
    STOCK_REQ,
    RECEIPT_CONFIRM,
    DISPATCH_NOTICE,
    REPLENISH_REQ,
    OTHER
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
                    std::uint32_t max_retries);
    ~message_handler();

    /**
     * Process a request slot and produce responses.
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

    auth_module& m_auth_module;
    inventory_manager& m_inventory_manager;
    std::uint32_t m_ack_timeout_seconds;
    std::uint32_t m_max_retries;
};

#endif // MESSAGE_HANDLER_HPP
