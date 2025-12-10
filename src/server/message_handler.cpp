#include "message_handler.hpp"

#include <cstring>
#include <iostream>

message_handler::message_handler(auth_module& auth, session_manager& session_mgr, timer_manager& timer_mgr,
                                 send_callback_t send_callback)
    : m_auth_module(auth), m_session_manager(session_mgr), m_timer_manager(timer_mgr),
      m_send_callback(send_callback)
{
}

message_handler::~message_handler()
{
}

bool message_handler::generate_ack_if_needed(const message_t& msg, const std::string& session_id, message_t* ack_out)
{
    if (!ack_out)
    {
        return false;
    }

    // Categorize message
    message_category category = categorize_message(msg.msg_type);
    
    // Only authenticated sessions can receive ACKs (except for their AUTH_REQUEST)
    if (category == message_category::AUTH_REQUEST || category == message_category::ACK_MESSAGE)
    {
        return false;
    }
    // As the message is not an AUTH_REQUEST or ACK_MESSAGE, it must be an authenticated session
    // if not, return false
    if (!m_session_manager.is_authenticated(session_id))
    {
        return false;
    }    
    // Given that the session is authenticated and the message is not an AUTH_REQUEST or ACK_MESSAGE,
    // we can generate an ACK for the message
    int create_result = create_acknowledgment_message(ack_out, SERVER, SERVER,
                                                      msg.source_role, msg.source_id,
                                                      msg.timestamp, 200);
    // if the ACK message was not created successfully, return false
    if (create_result != 0)
    {
        return false;
    }
    return true;
}

message_processing_result message_handler::process_message(const std::string& json_input, const std::string& session_id)
{
    message_processing_result result;
    result.success = false;
    result.should_send_response = false;
    result.error_message = "";

    // Reject messages from blacklisted sessions
    if (m_session_manager.is_blacklisted(session_id))
    {
        result.error_message = "Session is blacklisted";
        return result;
    }

    // Deserialize the incoming message
    message_t incoming_msg;
    int deserialize_result = deserialize_message_from_json(json_input.c_str(), &incoming_msg);
    if (deserialize_result != 0)
    {
        result.error_message = "Failed to deserialize message";
        return result;
    }

    // Categorize message for routing
    message_category category = categorize_message(incoming_msg.msg_type);
    
    // SECURITY: Only AUTH_REQUEST is allowed from unauthenticated sessions
    if (category != message_category::AUTH_REQUEST && !m_session_manager.is_authenticated(session_id))
    {
        result.error_message = "Message rejected: session not authenticated";
        std::cout << "[SECURITY] Rejected unauthenticated message of type: " << incoming_msg.msg_type << std::endl;
        return result;
    }
    
    // Route message to appropriate handler
    switch (category)
    {
        case message_category::AUTH_REQUEST:
            return handle_auth_request(incoming_msg, session_id);
        
        case message_category::ACK_MESSAGE:
            return handle_ack_message(incoming_msg, session_id);
        
        case message_category::OTHER:
        default:
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
    int create_result = create_auth_response_message(&response_msg, msg.source_role, msg.source_id,
                                                      static_cast<int>(auth_res.status_code));

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
        
        // Track AUTH_RESPONSE for ACK (only for successful auth)
        track_message_for_ack(session_id, response_msg);
    }

    return result;
}

message_processing_result message_handler::handle_other_message(const message_t& msg, const std::string& session_id)
{
    message_processing_result result;
    result.success = true;
    result.should_send_response = false;
    result.error_message = "";
    
    // TODO: Process other message
    // For now, just log that we received it
    std::cout << "[MSG] Processing other message type: " << msg.msg_type << " from " << msg.source_id << std::endl;
    
    return result;
}

message_category message_handler::categorize_message(const char* msg_type) const
{
    // Check for AUTH_REQUEST messages
    if (strcmp(msg_type, HUB_TO_SERVER__AUTH_REQUEST) == 0 ||
        strcmp(msg_type, WAREHOUSE_TO_SERVER__AUTH_REQUEST) == 0)
    {
        return message_category::AUTH_REQUEST;
    }
    
    // Check for ACK messages
    if (strcmp(msg_type, HUB_TO_SERVER__ACK) == 0 ||
        strcmp(msg_type, WAREHOUSE_TO_SERVER__ACK) == 0)
    {
        return message_category::ACK_MESSAGE;
    }
    
    // All other messages
    return message_category::OTHER;
}

message_processing_result message_handler::handle_ack_message(const message_t& msg, const std::string& session_id)
{
    message_processing_result result;
    result.success = true;
    result.should_send_response = false;
    result.error_message = "";

    // Authentication already verified in process_message()
    
    // Extract ACK information
    std::string ack_for_timestamp(msg.payload.acknowledgment.ack_for_timestamp);
    int status_code = msg.payload.acknowledgment.status_code;

    std::cout << "[ACK] Received ACK from " << msg.source_id 
              << " for timestamp: " << ack_for_timestamp 
              << " (status: " << status_code << ")" << std::endl;

    // Cancel timer - if it doesn't exist, it's a duplicate or unknown ACK
    if (m_timer_manager.cancel_ack_timer(session_id, ack_for_timestamp))
    {
        std::cout << "[ACK] Successfully processed and cancelled timer" << std::endl;
    }
    else
    {
        // Duplicate ACK detection: ACK for message we're not tracking
        // either its a duplicate ack for a message that already got acked
        // or its an unknown message that we're not tracking
        // or the ack corresponds to a message that exceeded the max retries
        std::cout << "[ACK] WARNING: Received duplicate, expired or unknown ACK (timestamp: " 
                  << ack_for_timestamp << ") - discarding" << std::endl;
    }
    
    return result;
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

// ==================== ACK TRACKING METHODS ====================
// Note: timer_manager is the source of truth for tracking
// It has a map of session_id to a map of message_timestamp to a timer
// The timer is started recursively with a retry count
// The timer is cancelled when the ACK is received
// The timer is restarted with a incremented retry count when the message is resent

void message_handler::track_message_for_ack(const std::string& session_id, const message_t& msg)
{
    std::cout << "[ACK_TRACKING] Started tracking message: " << msg.timestamp 
              << " for session: " << session_id << std::endl;
    
    // Start recursive timer with retry_count = 0
    start_ack_timer_recursive(session_id, msg, 0);
}

void message_handler::start_ack_timer_recursive(const std::string& session_id, 
                                                 const message_t& msg, 
                                                 int retry_count)
{
    // Lambda captures all necessary data: session_id, msg, retry_count
    m_timer_manager.start_ack_timer(session_id, msg.timestamp, SERVER_ACK_TIMEOUT_SECONDS,
        [this, session_id, msg, retry_count]() {
            // Timer expired - check if we should retry or give up
            if (retry_count >= SERVER_MAX_RETRIES - 1)
            {
                // Max retries reached - close session
                std::cout << "[ACK_TIMEOUT] Max retries (" << SERVER_MAX_RETRIES 
                          << ") reached for session: " << session_id 
                          << ", message: " << msg.timestamp << " - closing session" << std::endl;
                
                // Clear all timers for this session
                m_timer_manager.clear_session_timers(session_id);
                
                // blacklist the session
                m_session_manager.blacklist_session(session_id);
            }
            else
            {
                // Retry: resend message and restart timer
                std::cout << "[ACK_TIMEOUT] Timeout for message " << msg.timestamp 
                          << " (retry " << (retry_count + 1) << "/" << SERVER_MAX_RETRIES << ")" << std::endl;
                
                // Serialize and resend the message
                char json[BUFFER_SIZE];
                if (serialize_message_to_json(&msg, json) == 0)
                {
                    m_send_callback(session_id, std::string(json));
                    std::cout << "[ACK_RETRY] Message resent to session: " << session_id << std::endl;
                }
                else
                {
                    std::cerr << "[ACK_RETRY] ERROR: Failed to serialize message for retry" << std::endl;
                }
                
                start_ack_timer_recursive(session_id, msg, retry_count + 1);
                
            }
        });
}
