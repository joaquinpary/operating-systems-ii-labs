#include "message_handler.hpp"

#include <cstring>
#include <iostream>

message_handler::message_handler(auth_module& auth, session_manager& session_mgr, timer_manager& timer_mgr,
                                 inventory_manager& inv_mgr, send_callback_t send_callback)
    : m_auth_module(auth), m_session_manager(session_mgr), m_timer_manager(timer_mgr), m_inventory_manager(inv_mgr),
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
    int create_result =
        create_acknowledgment_message(ack_out, SERVER, SERVER, msg.source_role, msg.source_id, msg.timestamp, 200);
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

        return handle_other_message(incoming_msg, session_id, category);
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

        // Send AUTH_RESPONSE via callback first (goes into write queue first)
        char auth_json[BUFFER_SIZE];
        if (serialize_message_to_json(&response_msg, auth_json) == 0)
        {
            m_send_callback(session_id, std::string(auth_json));
            track_message_for_ack(session_id, response_msg);
        }

        // Build inventory message — returned as result.response_message (enqueued second)
        message_t inv_msg;
        if (m_inventory_manager.get_client_inventory_message(auth_res.username, auth_res.client_type, inv_msg))
        {
            result.response_message = inv_msg; // Replaces auth response in result
            track_message_for_ack(session_id, inv_msg);
            std::cout << "[MSG_HANDLER] Auth response sent, inventory queued for " << auth_res.username << std::endl;
        }
        else
        {
            // No inventory to send, don't send the auth response twice
            result.should_send_response = false;
        }
    }

    return result;
}

message_processing_result message_handler::handle_other_message(const message_t& msg, const std::string& session_id,
                                                                message_category category)
{
    message_processing_result result;
    result.success = true;
    result.should_send_response = false;
    result.error_message = "";

    std::cout << "[MSG] Processing message type: " << msg.msg_type << " from " << msg.source_id << std::endl;

    if (category == message_category::INV_UPDATE)
    {
        std::vector<stock_request_result> fulfilled_orders = m_inventory_manager.handle_inventory_update(msg);

        // Send dispatch messages for any pending orders that were just fulfilled
        for (const auto& fulfilled : fulfilled_orders)
        {
            // Create dispatch message for the assigned warehouse
            message_t dispatch_msg;
            int create_result = create_items_message(&dispatch_msg, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB,
                                                     SERVER, fulfilled.assigned_warehouse_id.c_str(), fulfilled.items,
                                                     fulfilled.item_count, NULL);

            if (create_result != 0)
            {
                std::cerr << "[MSG] ERROR: Failed to create dispatch message for pending order "
                          << fulfilled.transaction_id << std::endl;
                continue;
            }

            std::string warehouse_session = m_session_manager.find_session_by_username(fulfilled.assigned_warehouse_id);

            if (warehouse_session.empty())
            {
                std::cerr << "[MSG] WARNING: Warehouse " << fulfilled.assigned_warehouse_id
                          << " not connected, pending order dispatch deferred" << std::endl;
                continue;
            }

            char dispatch_json[BUFFER_SIZE];
            if (serialize_message_to_json(&dispatch_msg, dispatch_json) == 0)
            {
                m_send_callback(warehouse_session, std::string(dispatch_json));
                track_message_for_ack(warehouse_session, dispatch_msg);
                std::cout << "[MSG] Sent dispatch for fulfilled pending order " << fulfilled.transaction_id
                          << " to warehouse " << fulfilled.assigned_warehouse_id << std::endl;
            }
        }
    }
    else if (category == message_category::STOCK_REQ)
    {
        stock_request_result stock_result = m_inventory_manager.handle_stock_request(msg);

        if (!stock_result.success)
        {
            result.success = false;
            result.error_message = "Failed to process stock request";
            return result;
        }

        // If a warehouse was assigned for a HUB request, send dispatch order to warehouse
        if (stock_result.warehouse_assigned && !stock_result.requesting_hub_id.empty())
        {
            // Create SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB message
            message_t dispatch_msg;
            int create_result =
                create_items_message(&dispatch_msg, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB,
                                     SERVER,                                     // source_id = SERVER
                                     stock_result.assigned_warehouse_id.c_str(), // target_id = warehouse
                                     stock_result.items, stock_result.item_count, NULL);

            if (create_result != 0)
            {
                std::cerr << "[MSG] ERROR: Failed to create dispatch message for warehouse "
                          << stock_result.assigned_warehouse_id << std::endl;
                return result; // stock request was still created successfully
            }

            // Find warehouse session and send
            std::string warehouse_session =
                m_session_manager.find_session_by_username(stock_result.assigned_warehouse_id);

            if (warehouse_session.empty())
            {
                std::cerr << "[MSG] WARNING: Warehouse " << stock_result.assigned_warehouse_id
                          << " is not connected, dispatch message cannot be sent now" << std::endl;
                // Transaction is still in DB as DISPATCHED — could be retried later
            }
            else
            {
                char dispatch_json[BUFFER_SIZE];
                if (serialize_message_to_json(&dispatch_msg, dispatch_json) == 0)
                {
                    m_send_callback(warehouse_session, std::string(dispatch_json));
                    track_message_for_ack(warehouse_session, dispatch_msg);
                    std::cout << "[MSG] Sent dispatch order to warehouse " << stock_result.assigned_warehouse_id
                              << " (session: " << warehouse_session << ")" << std::endl;
                }
                else
                {
                    std::cerr << "[MSG] ERROR: Failed to serialize dispatch message" << std::endl;
                }
            }
        }
    }
    else if (category == message_category::RECEIPT_CONFIRM)
    {
        m_inventory_manager.handle_receipt_confirmation(msg);
    }
    else if (category == message_category::DISPATCH_NOTICE)
    {
        m_inventory_manager.handle_shipment_notice(msg);
    }
    else if (category == message_category::REPLENISH_REQ)
    {
        stock_request_result replenish_result = m_inventory_manager.handle_replenish_request(msg);

        if (!replenish_result.success)
        {
            result.success = false;
            result.error_message = "Failed to process replenish request";
            return result;
        }

        // Send RESTOCK_NOTICE back to the warehouse to trigger its internal restock workflow
        message_t restock_msg;
        int create_result = create_items_message(&restock_msg, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE, SERVER,
                                                 replenish_result.assigned_warehouse_id.c_str(), replenish_result.items,
                                                 replenish_result.item_count, NULL);

        if (create_result != 0)
        {
            std::cerr << "[MSG] ERROR: Failed to create restock notice for warehouse "
                      << replenish_result.assigned_warehouse_id << std::endl;
            return result;
        }

        char restock_json[BUFFER_SIZE];
        if (serialize_message_to_json(&restock_msg, restock_json) == 0)
        {
            m_send_callback(session_id, std::string(restock_json));
            track_message_for_ack(session_id, restock_msg);
            std::cout << "[MSG] Sent restock notice to warehouse " << replenish_result.assigned_warehouse_id
                      << std::endl;
        }
        else
        {
            std::cerr << "[MSG] ERROR: Failed to serialize restock notice" << std::endl;
        }
    }
    else
    {
        std::cout << "[MSG] Received message of unhandled type: " << msg.msg_type << std::endl;
    }

    return result;
}

message_category message_handler::categorize_message(const char* msg_type) const
{
    // Check for AUTH_REQUEST messages
    if (strcmp(msg_type, HUB_TO_SERVER__AUTH_REQUEST) == 0 || strcmp(msg_type, WAREHOUSE_TO_SERVER__AUTH_REQUEST) == 0)
    {
        return message_category::AUTH_REQUEST;
    }

    // Check for ACK messages
    if (strcmp(msg_type, HUB_TO_SERVER__ACK) == 0 || strcmp(msg_type, WAREHOUSE_TO_SERVER__ACK) == 0)
    {
        return message_category::ACK_MESSAGE;
    }

    if (strcmp(msg_type, HUB_TO_SERVER__INVENTORY_UPDATE) == 0 ||
        strcmp(msg_type, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE) == 0)
    {
        return message_category::INV_UPDATE;
    }

    if (strcmp(msg_type, HUB_TO_SERVER__STOCK_REQUEST) == 0)
    {
        return message_category::STOCK_REQ;
    }

    if (strcmp(msg_type, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION) == 0)
    {
        return message_category::RECEIPT_CONFIRM;
    }

    if (strcmp(msg_type, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE) == 0)
    {
        return message_category::DISPATCH_NOTICE;
    }

    if (strcmp(msg_type, WAREHOUSE_TO_SERVER__REPLENISH_REQUEST) == 0)
    {
        return message_category::REPLENISH_REQ;
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

    std::cout << "[ACK] Received ACK from " << msg.source_id << " for timestamp: " << ack_for_timestamp
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
        std::cout << "[ACK] WARNING: Received duplicate, expired or unknown ACK (timestamp: " << ack_for_timestamp
                  << ") - discarding" << std::endl;
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
// should use track_message_for_ack() when messages are sent that expect an ACK

void message_handler::track_message_for_ack(const std::string& session_id, const message_t& msg)
{
    std::cout << "[ACK_TRACKING] Started tracking message: " << msg.timestamp << " for session: " << session_id
              << std::endl;

    // Start recursive timer with retry_count = 0
    start_ack_timer_recursive(session_id, msg, 0);
}

void message_handler::start_ack_timer_recursive(const std::string& session_id, const message_t& msg, int retry_count)
{
    // Lambda captures all necessary data: session_id, msg, retry_count
    m_timer_manager.start_ack_timer(
        session_id, msg.timestamp, SERVER_ACK_TIMEOUT_SECONDS, [this, session_id, msg, retry_count]() {
            // Timer expired - check if we should retry or give up
            if (retry_count >= SERVER_MAX_RETRIES - 1)
            {
                // Max retries reached - close session
                std::cout << "[ACK_TIMEOUT] Max retries (" << SERVER_MAX_RETRIES
                          << ") reached for session: " << session_id << ", message: " << msg.timestamp
                          << " - closing session" << std::endl;

                // Clear all timers for this session
                m_timer_manager.clear_session_timers(session_id);

                // blacklist the session
                m_session_manager.blacklist_session(session_id);
            }
            else
            {
                // Retry: resend message and restart timer
                std::cout << "[ACK_TIMEOUT] Timeout for message " << msg.timestamp << " (retry " << (retry_count + 1)
                          << "/" << SERVER_MAX_RETRIES << ")" << std::endl;

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
