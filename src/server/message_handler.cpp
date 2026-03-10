#include "message_handler.hpp"

#include <cstring>
#include <iostream>

// ==================== CONSTRUCTOR / DESTRUCTOR ====================

message_handler::message_handler(auth_module& auth, inventory_manager& inv_mgr, std::uint32_t ack_timeout_seconds,
                                 std::uint32_t max_retries)
    : m_auth_module(auth), m_inventory_manager(inv_mgr), m_ack_timeout_seconds(ack_timeout_seconds),
      m_max_retries(max_retries)
{
}

message_handler::~message_handler()
{
}

// ==================== RESPONSE SLOT HELPERS ====================

response_slot_t message_handler::make_send_response(const char* session_id, const message_t& msg)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);

    char json_buf[BUFFER_SIZE];
    if (serialize_message_to_json(&msg, json_buf) == 0)
    {
        std::uint32_t len = static_cast<std::uint32_t>(std::strlen(json_buf));
        std::memcpy(slot.payload, json_buf, len);
        slot.payload_len = len;
    }
    return slot;
}

response_slot_t message_handler::make_send_to_username(const char* username, const message_t& msg)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::SEND);
    // session_id left empty — reactor resolves via target_username
    std::strncpy(slot.target_username, username, CREDENTIALS_SIZE - 1);

    char json_buf[BUFFER_SIZE];
    if (serialize_message_to_json(&msg, json_buf) == 0)
    {
        std::uint32_t len = static_cast<std::uint32_t>(std::strlen(json_buf));
        std::memcpy(slot.payload, json_buf, len);
        slot.payload_len = len;
    }
    return slot;
}

response_slot_t message_handler::make_start_timer(const char* session_id, const message_t& msg)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::START_ACK_TIMER);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    std::strncpy(slot.timer_key, msg.timestamp, TIMESTAMP_SIZE - 1);
    slot.timer_timeout = m_ack_timeout_seconds;
    slot.retry_count = 0;
    slot.max_retries = m_max_retries;

    // Store serialized message in payload so reactor can resend on timeout
    char json_buf[BUFFER_SIZE];
    if (serialize_message_to_json(&msg, json_buf) == 0)
    {
        std::uint32_t len = static_cast<std::uint32_t>(std::strlen(json_buf));
        std::memcpy(slot.payload, json_buf, len);
        slot.payload_len = len;
    }
    return slot;
}

response_slot_t message_handler::make_cancel_timer(const char* session_id, const char* timestamp)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::CANCEL_ACK_TIMER);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    std::strncpy(slot.timer_key, timestamp, TIMESTAMP_SIZE - 1);
    return slot;
}

response_slot_t message_handler::make_clear_timers(const char* session_id)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::CLEAR_TIMERS);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    return slot;
}

response_slot_t message_handler::make_blacklist(const char* session_id)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::BLACKLIST);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    return slot;
}

response_slot_t message_handler::make_mark_authenticated(const char* session_id, const char* client_type,
                                                         const char* username)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    std::strncpy(slot.client_type, client_type, ROLE_SIZE - 1);
    std::strncpy(slot.username, username, CREDENTIALS_SIZE - 1);
    return slot;
}

// ==================== ACK GENERATION ====================

bool message_handler::generate_ack_if_needed(const message_t& msg, const request_slot_t& req, response_slot_t& ack_out)
{
    message_category category = categorize_message(msg.msg_type);

    // AUTH_REQUEST and ACK messages don't get ACKed
    if (category == message_category::AUTH_REQUEST || category == message_category::ACK_MESSAGE)
    {
        return false;
    }

    // Only authenticated sessions receive ACKs
    if (!req.is_authenticated)
    {
        return false;
    }

    message_t ack_msg;
    int create_result =
        create_acknowledgment_message(&ack_msg, SERVER, SERVER, msg.source_role, msg.source_id, msg.timestamp, 200);
    if (create_result != 0)
    {
        return false;
    }

    ack_out = make_send_response(req.session_id, ack_msg);
    return true;
}

// ==================== MAIN ENTRY POINT ====================

std::vector<response_slot_t> message_handler::process_request(const request_slot_t& request)
{
    std::vector<response_slot_t> responses;

    // Handle disconnect notifications (reactor signals client disconnected)
    if (request.is_disconnect)
    {
        std::string username(request.username);
        if (!username.empty())
        {
            std::cout << "[WORKER] Processing disconnect for " << username << std::endl;
            m_auth_module.deactivate_client(username);
        }
        return responses;
    }

    // Reject messages from blacklisted sessions
    if (request.is_blacklisted)
    {
        std::cout << "[WORKER] Ignoring message from blacklisted session: " << request.session_id << std::endl;
        return responses;
    }

    // Deserialize the incoming message
    message_t incoming_msg;
    int deserialize_result = deserialize_message_from_json(request.raw_json, &incoming_msg);
    if (deserialize_result != 0)
    {
        std::cerr << "[WORKER] Failed to deserialize message from " << request.session_id << std::endl;
        return responses;
    }

    // Categorize message for routing
    message_category category = categorize_message(incoming_msg.msg_type);

    // SECURITY: Only AUTH_REQUEST is allowed from unauthenticated sessions
    if (category != message_category::AUTH_REQUEST && !request.is_authenticated)
    {
        std::cout << "[SECURITY] Rejected unauthenticated message type: " << incoming_msg.msg_type << " from "
                  << request.session_id << std::endl;
        return responses;
    }

    // Generate ACK first (so it's sent before the processing response)
    response_slot_t ack_slot;
    if (generate_ack_if_needed(incoming_msg, request, ack_slot))
    {
        responses.push_back(ack_slot);
    }

    // Route message to appropriate handler
    std::vector<response_slot_t> handler_responses;
    switch (category)
    {
    case message_category::AUTH_REQUEST:
        handler_responses = handle_auth_request(incoming_msg, request);
        break;
    case message_category::ACK_MESSAGE:
        handler_responses = handle_ack_message(incoming_msg, request);
        break;
    default:
        handler_responses = handle_other_message(incoming_msg, request, category);
        break;
    }

    responses.insert(responses.end(), handler_responses.begin(), handler_responses.end());
    return responses;
}

// ==================== AUTH REQUEST ====================

std::vector<response_slot_t> message_handler::handle_auth_request(const message_t& msg, const request_slot_t& req)
{
    std::vector<response_slot_t> responses;

    // Extract credentials
    std::string username(msg.payload.client_auth_request.username);
    std::string password(msg.payload.client_auth_request.password);

    // Authenticate using auth_module
    auth_result auth_res = m_auth_module.authenticate(username, password);

    // Create response message
    message_t response_msg;
    std::memset(&response_msg, 0, sizeof(response_msg));

    const char* response_type = get_auth_response_type(msg.source_role);
    if (!response_type)
    {
        std::cerr << "[WORKER] Invalid source role for auth request" << std::endl;
        return responses;
    }

    int create_result = create_auth_response_message(&response_msg, msg.source_role, msg.source_id,
                                                     static_cast<int>(auth_res.status_code));
    if (create_result != 0)
    {
        std::cerr << "[WORKER] Failed to create auth response" << std::endl;
        return responses;
    }

    std::strncpy(response_msg.msg_type, response_type, MESSAGE_TYPE_SIZE - 1);
    response_msg.msg_type[MESSAGE_TYPE_SIZE - 1] = '\0';

    if (auth_res.status_code == auth_result_code::SUCCESS)
    {
        // 1. Mark session as authenticated (reactor will update session_manager)
        responses.push_back(
            make_mark_authenticated(req.session_id, auth_res.client_type.c_str(), auth_res.username.c_str()));

        // 2. Send AUTH_RESPONSE
        responses.push_back(make_send_response(req.session_id, response_msg));
        responses.push_back(make_start_timer(req.session_id, response_msg));

        // 3. Build and send inventory message
        message_t inv_msg;
        if (m_inventory_manager.get_client_inventory_message(auth_res.username, auth_res.client_type, inv_msg))
        {
            responses.push_back(make_send_response(req.session_id, inv_msg));
            responses.push_back(make_start_timer(req.session_id, inv_msg));
            std::cout << "[WORKER] Auth success + inventory queued for " << auth_res.username << std::endl;
        }
    }
    else
    {
        // Auth failed — just send the response (no timer tracking needed)
        responses.push_back(make_send_response(req.session_id, response_msg));
    }

    return responses;
}

// ==================== ACK MESSAGE ====================

std::vector<response_slot_t> message_handler::handle_ack_message(const message_t& msg, const request_slot_t& req)
{
    std::vector<response_slot_t> responses;

    std::string ack_for_timestamp(msg.payload.acknowledgment.ack_for_timestamp);
    int status_code = msg.payload.acknowledgment.status_code;

    std::cout << "[ACK] Received ACK from " << msg.source_id << " for timestamp: " << ack_for_timestamp
              << " (status: " << status_code << ")" << std::endl;

    // Tell reactor to cancel the ACK timer
    responses.push_back(make_cancel_timer(req.session_id, ack_for_timestamp.c_str()));

    return responses;
}

// ==================== OTHER MESSAGES ====================

std::vector<response_slot_t> message_handler::handle_other_message(const message_t& msg, const request_slot_t& req,
                                                                   message_category category)
{
    std::vector<response_slot_t> responses;

    std::cout << "[MSG] Processing message type: " << msg.msg_type << " from " << msg.source_id << std::endl;

    if (category == message_category::INV_UPDATE)
    {
        std::vector<stock_request_result> fulfilled_orders = m_inventory_manager.handle_inventory_update(msg);

        for (const auto& fulfilled : fulfilled_orders)
        {
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

            // Send to warehouse by username (reactor resolves session)
            responses.push_back(make_send_to_username(fulfilled.assigned_warehouse_id.c_str(), dispatch_msg));
            // Timer tracking: we need the warehouse session_id, but we don't have it.
            // Timer will be started when reactor resolves the username to session_id.
            // For now, we skip timer tracking for dispatches by username — reactor handles it.
            // Alternative: add a START_ACK_TIMER_BY_USERNAME command.
            std::cout << "[MSG] Dispatch queued for warehouse " << fulfilled.assigned_warehouse_id << std::endl;
        }
    }
    else if (category == message_category::STOCK_REQ)
    {
        stock_request_result stock_result = m_inventory_manager.handle_stock_request(msg);

        if (!stock_result.success)
        {
            std::cerr << "[MSG] Failed to process stock request" << std::endl;
            return responses;
        }

        if (stock_result.warehouse_assigned && !stock_result.requesting_hub_id.empty())
        {
            message_t dispatch_msg;
            int create_result = create_items_message(&dispatch_msg, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB,
                                                     SERVER, stock_result.assigned_warehouse_id.c_str(),
                                                     stock_result.items, stock_result.item_count, NULL);
            if (create_result != 0)
            {
                std::cerr << "[MSG] ERROR: Failed to create dispatch message for warehouse "
                          << stock_result.assigned_warehouse_id << std::endl;
                return responses;
            }

            responses.push_back(make_send_to_username(stock_result.assigned_warehouse_id.c_str(), dispatch_msg));
            std::cout << "[MSG] Dispatch order queued for warehouse " << stock_result.assigned_warehouse_id
                      << std::endl;
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
            std::cerr << "[MSG] Failed to process replenish request" << std::endl;
            return responses;
        }

        message_t restock_msg;
        int create_result = create_items_message(&restock_msg, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE, SERVER,
                                                 replenish_result.assigned_warehouse_id.c_str(), replenish_result.items,
                                                 replenish_result.item_count, NULL);
        if (create_result != 0)
        {
            std::cerr << "[MSG] ERROR: Failed to create restock notice" << std::endl;
            return responses;
        }

        // Restock goes back to the same session that sent the replenish request
        responses.push_back(make_send_response(req.session_id, restock_msg));
        responses.push_back(make_start_timer(req.session_id, restock_msg));
        std::cout << "[MSG] Restock notice queued for warehouse " << replenish_result.assigned_warehouse_id
                  << std::endl;
    }
    else
    {
        std::cout << "[MSG] Received message of unhandled type: " << msg.msg_type << std::endl;
    }

    return responses;
}

// ==================== CATEGORIZATION ====================

message_category message_handler::categorize_message(const char* msg_type) const
{
    if (std::strcmp(msg_type, HUB_TO_SERVER__AUTH_REQUEST) == 0 ||
        std::strcmp(msg_type, WAREHOUSE_TO_SERVER__AUTH_REQUEST) == 0)
    {
        return message_category::AUTH_REQUEST;
    }

    if (std::strcmp(msg_type, HUB_TO_SERVER__ACK) == 0 || std::strcmp(msg_type, WAREHOUSE_TO_SERVER__ACK) == 0)
    {
        return message_category::ACK_MESSAGE;
    }

    if (std::strcmp(msg_type, HUB_TO_SERVER__INVENTORY_UPDATE) == 0 ||
        std::strcmp(msg_type, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE) == 0)
    {
        return message_category::INV_UPDATE;
    }

    if (std::strcmp(msg_type, HUB_TO_SERVER__STOCK_REQUEST) == 0)
    {
        return message_category::STOCK_REQ;
    }

    if (std::strcmp(msg_type, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION) == 0)
    {
        return message_category::RECEIPT_CONFIRM;
    }

    if (std::strcmp(msg_type, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE) == 0)
    {
        return message_category::DISPATCH_NOTICE;
    }

    if (std::strcmp(msg_type, WAREHOUSE_TO_SERVER__REPLENISH_REQUEST) == 0)
    {
        return message_category::REPLENISH_REQ;
    }

    return message_category::OTHER;
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
