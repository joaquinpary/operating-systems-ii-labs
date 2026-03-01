#ifndef MESSAGE_HANDLER_HPP
#define MESSAGE_HANDLER_HPP

#include "auth_module.hpp"
#include "inventory_manager.hpp"
#include "session_manager.hpp"
#include "timer_manager.hpp"
#include <common/json_manager.h>
#include <cstdint>
#include <string>

struct message_processing_result
{
    bool success;
    message_t response_message; // Response to send back (if any)
    bool should_send_response;
    std::string error_message;
};

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

class message_handler
{
  public:
    using send_callback_t = std::function<void(const std::string& session_id, const std::string& data)>;

    message_handler(auth_module& auth, session_manager& session_mgr, timer_manager& timer_mgr,
                    inventory_manager& inv_mgr, std::uint32_t ack_timeout_seconds,
                    std::uint32_t max_retries, send_callback_t send_callback);
    ~message_handler();

    // Generate ACK if the message requires it (called before processing for immediate response)
    // Returns true if ACK was generated, false otherwise
    bool generate_ack_if_needed(const message_t& msg, const std::string& session_id, message_t* ack_out);

    // Process an incoming message (without generating ACK - that's done separately)
    // Returns processing result with response message if needed
    message_processing_result process_message(const std::string& json_input, const std::string& session_id);

  private:
    // Handle authentication request
    message_processing_result handle_auth_request(const message_t& msg, const std::string& session_id);

    // Handle ACK messages from clients
    message_processing_result handle_ack_message(const message_t& msg, const std::string& session_id);

    // Handle other message types (for now, reject if not authenticated)
    message_processing_result handle_other_message(const message_t& msg, const std::string& session_id,
                                                   message_category category);

    // Categorize message type for routing
    message_category categorize_message(const char* msg_type) const;

    // Get response message type based on client type
    const char* get_auth_response_type(const std::string& client_type) const;

    // ACK tracking methods (timer_manager is the source of truth)
    void track_message_for_ack(const std::string& session_id, const message_t& msg);
    void start_ack_timer_recursive(const std::string& session_id, const message_t& msg, int retry_count);

    auth_module& m_auth_module;
    session_manager& m_session_manager;
    timer_manager& m_timer_manager;
    inventory_manager& m_inventory_manager;
    std::uint32_t m_ack_timeout_seconds;
    std::uint32_t m_max_retries;
    send_callback_t m_send_callback;
};

#endif // MESSAGE_HANDLER_HPP
