#ifndef MESSAGE_HANDLER_HPP
#define MESSAGE_HANDLER_HPP

#include "auth_module.hpp"
#include "session_manager.hpp"
#include <common/json_manager.h>
#include <memory>
#include <string>

struct message_processing_result
{
    bool success;
    message_t response_message; // Response to send back (if any)
    bool should_send_response;
    std::string error_message;
};

class message_handler
{
  public:
    message_handler(auth_module& auth, session_manager& session_mgr);
    ~message_handler();

    // Process an incoming message
    // Returns processing result with response message if needed
    message_processing_result process_message(const std::string& json_input, const std::string& session_id);

  private:
    // Handle authentication request
    message_processing_result handle_auth_request(const message_t& msg, const std::string& session_id);

    // Handle other message types (for now, reject if not authenticated)
    message_processing_result handle_other_message(const message_t& msg, const std::string& session_id);

    // Check if message type is an auth request
    bool is_auth_request(const char* msg_type) const;

    // Get response message type based on client type
    const char* get_auth_response_type(const std::string& client_type) const;

    auth_module& m_auth_module;
    session_manager& m_session_manager;
};

#endif // MESSAGE_HANDLER_HPP
