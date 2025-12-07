#include "message_handler.hpp"

#include <cstring>
#include <iostream>

message_handler::message_handler(auth_module& auth, session_manager& session_mgr)
    : m_auth_module(auth), m_session_manager(session_mgr)
{
}

message_handler::~message_handler()
{
}

message_processing_result message_handler::process_message(const std::string& json_input, const std::string& session_id)
{
    message_processing_result result;
    result.success = false;
    result.should_send_response = false;
    result.error_message = "";

    // Deserialize the incoming message
    message_t incoming_msg;
    int deserialize_result = deserialize_message_from_json(json_input.c_str(), &incoming_msg);
    if (deserialize_result != 0)
    {
        result.error_message = "Failed to deserialize message";
        return result;
    }

    // Route message based on type
    if (is_auth_request(incoming_msg.msg_type))
    {
        return handle_auth_request(incoming_msg, session_id);
    }
    else
    {
        return handle_other_message(incoming_msg, session_id);
    }
}

message_processing_result message_handler::handle_auth_request(const message_t& msg, const std::string& session_id)
{
    message_processing_result result;
    result.success = false;
    result.should_send_response = true;
    result.error_message = "";

    // Extract username and password from payload
    std::string username(msg.payload.client_auth_request.username);
    std::string password(msg.payload.client_auth_request.password);

    // Authenticate using auth_module
    auth_result auth_res = m_auth_module.authenticate(username, password);

    // Create response message
    message_t response_msg;
    memset(&response_msg, 0, sizeof(response_msg));

    // Determine response message type based on source role
    const char* response_type = get_auth_response_type(msg.source_role);
    if (!response_type)
    {
        result.error_message = "Invalid source role for auth request";
        return result;
    }

    // Create AUTH_RESPONSE message
    int create_result = create_status_message(&response_msg, SERVER, "server", msg.source_role, msg.source_id,
                                              AUTH_RESPONSE, static_cast<int>(auth_res.status_code));

    if (create_result != 0)
    {
        result.error_message = "Failed to create auth response message";
        return result;
    }

    // Override msg_type with the correct response type
    strncpy(response_msg.msg_type, response_type, MESSAGE_TYPE_SIZE - 1);
    response_msg.msg_type[MESSAGE_TYPE_SIZE - 1] = '\0';

    result.response_message = response_msg;
    result.success = true;

    // If authentication successful, mark session as authenticated
    if (auth_res.status_code == auth_result_code::SUCCESS)
    {
        m_session_manager.mark_authenticated(session_id, auth_res.client_type, auth_res.username);
    }

    return result;
}

message_processing_result message_handler::handle_other_message(const message_t& msg, const std::string& session_id)
{
    message_processing_result result;
    result.success = false;
    result.should_send_response = false;
    result.error_message = "";

    // Check if session is authenticated
    if (!m_session_manager.is_authenticated(session_id))
    {
        // Reject unauthenticated messages (for now, just ignore them)
        result.error_message = "Message rejected: session not authenticated";
        std::cout << "Rejected unauthenticated message of type: " << msg.msg_type << std::endl;
        return result;
    }

    // TODO: Process other message types here
    // For now, just acknowledge that we received it
    result.success = true;
    result.should_send_response = false;
    return result;
}

bool message_handler::is_auth_request(const char* msg_type) const
{
    return (strcmp(msg_type, HUB_TO_SERVER__AUTH_REQUEST) == 0 ||
            strcmp(msg_type, WAREHOUSE_TO_SERVER__AUTH_REQUEST) == 0);
}

const char* message_handler::get_auth_response_type(const std::string& client_type) const
{
    if (client_type == HUB)
    {
        return SERVER_TO_HUB__AUTH_RESPONSE;
    }
    else if (client_type == WAREHOUSE)
    {
        return SERVER_TO_WAREHOUSE__AUTH_RESPONSE;
    }
    return nullptr;
}
