#include "message_handler.hpp"
#include "admin_cli_interface.h"

#include <common/logger.h>

#include <cstring>
#include <dlfcn.h>
#include <iostream>

// ==================== CONSTRUCTOR / DESTRUCTOR ====================

message_handler::message_handler(auth_module& auth, inventory_manager& inv_mgr, std::uint32_t ack_timeout_seconds,
                                 std::uint32_t max_retries, std::uint32_t keepalive_timeout_seconds,
                                 const std::string& db_conn_string)
    : m_auth_module(auth), m_inventory_manager(inv_mgr), m_ack_timeout_seconds(ack_timeout_seconds),
      m_max_retries(max_retries), m_keepalive_timeout_seconds(keepalive_timeout_seconds)
{
    // Load admin CLI plugin (best-effort: server works without it)
    m_admin_lib = dlopen("libadmin_cli.so", RTLD_LAZY);
    if (!m_admin_lib)
        m_admin_lib = dlopen("libadmin_cli.so.1", RTLD_LAZY);

    if (m_admin_lib)
    {
        using version_fn = const char* (*)();
        using init_fn = int (*)(const char*);

        auto ver = reinterpret_cast<version_fn>(dlsym(m_admin_lib, "admin_cli_version"));
        auto init = reinterpret_cast<init_fn>(dlsym(m_admin_lib, "admin_cli_init"));
        m_admin_handle = reinterpret_cast<admin_handle_fn>(dlsym(m_admin_lib, "admin_cli_handle"));
        m_admin_shutdown = reinterpret_cast<admin_shutdown_fn>(dlsym(m_admin_lib, "admin_cli_shutdown"));

        if (init && m_admin_handle && !db_conn_string.empty())
        {
            if (init(db_conn_string.c_str()) == 0)
            {
                LOG_INFO_MSG("[ADMIN] libadmin_cli.so loaded (v%s)", ver ? ver() : "?");
            }
            else
            {
                LOG_ERROR_MSG("[ADMIN] admin_cli_init failed");
                m_admin_handle = nullptr;
            }
        }
        else if (db_conn_string.empty())
        {
            LOG_WARNING_MSG("[ADMIN] libadmin_cli.so loaded but no conn_string — CLI disabled");
            m_admin_handle = nullptr;
        }
        else
        {
            LOG_ERROR_MSG("[ADMIN] libadmin_cli.so missing symbols");
            m_admin_handle = nullptr;
        }
    }
    else
    {
        LOG_WARNING_MSG("[ADMIN] libadmin_cli.so not found — CLI disabled (%s)", dlerror());
    }
}

message_handler::~message_handler()
{
    if (m_admin_shutdown)
        m_admin_shutdown();
    if (m_admin_lib)
        dlclose(m_admin_lib);
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

    // Populate timer fields so reactor can start ACK timer after resolving the session
    slot.start_ack_timer = true;
    std::strncpy(slot.timer_key, msg.timestamp, TIMESTAMP_SIZE - 1);
    slot.timer_timeout = m_ack_timeout_seconds;
    slot.retry_count = 0;
    slot.max_retries = m_max_retries;

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

response_slot_t message_handler::make_start_keepalive_timer(const char* session_id)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::START_KEEPALIVE_TIMER);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
    slot.timer_timeout = m_keepalive_timeout_seconds;
    return slot;
}

response_slot_t message_handler::make_reset_keepalive_timer(const char* session_id)
{
    response_slot_t slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.command = static_cast<std::uint8_t>(response_command::RESET_KEEPALIVE_TIMER);
    std::strncpy(slot.session_id, session_id, SESSION_ID_SIZE - 1);
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

// ==================== IMMEDIATE ACK GENERATION ====================

std::optional<response_slot_t> message_handler::generate_ack(const request_slot_t& request)
{
    // No ACK for disconnects, blacklisted, or unauthenticated sessions
    if (request.is_disconnect || request.is_blacklisted || !request.is_authenticated)
    {
        return std::nullopt;
    }

    // Deserialize just to check message type and build the ACK
    message_t incoming_msg;
    if (deserialize_message_from_json(request.raw_json, &incoming_msg) != 0)
    {
        return std::nullopt;
    }

    response_slot_t ack_slot;
    if (generate_ack_if_needed(incoming_msg, request, ack_slot))
    {
        return ack_slot;
    }

    return std::nullopt;
}

// ==================== MAIN ENTRY POINT ====================

std::vector<response_slot_t> message_handler::process_request(const request_slot_t& request)
{
    std::vector<response_slot_t> responses;
    std::string sess_id(request.session_id);

    // Handle disconnect notifications (reactor signals client disconnected)
    if (request.is_disconnect)
    {
        std::string username(request.username);
        if (!username.empty())
        {
            LOG_INFO_MSG("[WORKER] disconnect user=%s sess=%s", username.c_str(), request.session_id);
            m_auth_module.deactivate_client(username);
        }
        // Mark session as dead so queued messages behind this one are skipped
        if (!sess_id.empty())
        {
            std::unique_lock lock(m_dead_sessions_mutex);
            m_dead_sessions.insert(sess_id);
        }
        return responses;
    }

    // Skip messages for sessions already known to be dead (disconnect was
    // processed earlier). This avoids wasting DB queries on stale messages.
    if (!sess_id.empty())
    {
        std::shared_lock lock(m_dead_sessions_mutex);
        if (m_dead_sessions.count(sess_id))
        {
            LOG_DEBUG_MSG("[WORKER] skipped dead sess=%s", request.session_id);
            return responses;
        }
    }

    // Reject messages from blacklisted sessions
    if (request.is_blacklisted)
    {
        LOG_WARNING_MSG("[WORKER] blacklisted sess=%s", request.session_id);
        return responses;
    }

    // Deserialize the incoming message
    message_t incoming_msg;
    int deserialize_result = deserialize_message_from_json(request.raw_json, &incoming_msg);
    if (deserialize_result != 0)
    {
        std::cerr << "[WORKER] deserialize fail sess=" << request.session_id << '\n';
        LOG_ERROR_MSG("[WORKER] deserialize fail sess=%s", request.session_id);
        return responses;
    }

    // Categorize message for routing
    message_category category = categorize_message(incoming_msg.msg_type);

    // SECURITY: Only AUTH_REQUEST is allowed from unauthenticated sessions
    if (category != message_category::AUTH_REQUEST && !request.is_authenticated)
    {
        LOG_WARNING_MSG("[SECURITY] rejected type=%s sess=%s (unauthenticated)", incoming_msg.msg_type,
                        request.session_id);
        return responses;
    }

    // NOTE: ACK is no longer generated here — it is sent immediately by the worker
    // thread via generate_ack() BEFORE calling process_request(), so the client
    // receives the ACK without waiting for heavy business logic (DB queries, etc.).

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
    case message_category::KEEPALIVE_MSG:
        // Keepalive: ACK was already sent immediately, just reset the keepalive timer
        LOG_DEBUG_MSG("[MSG] keepalive from=%s sess=%s", incoming_msg.source_id, request.session_id);
        responses.push_back(make_reset_keepalive_timer(request.session_id));
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
        std::cerr << "[AUTH] bad source_role\n";
        LOG_ERROR_MSG("[AUTH] bad source_role sess=%s", req.session_id);
        return responses;
    }

    int create_result = create_auth_response_message(&response_msg, msg.source_role, msg.source_id,
                                                     static_cast<int>(auth_res.status_code));
    if (create_result != 0)
    {
        std::cerr << "[AUTH] create response fail\n";
        LOG_ERROR_MSG("[AUTH] create response fail sess=%s", req.session_id);
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
            LOG_INFO_MSG("[AUTH] OK user=%s type=%s sess=%s", auth_res.username.c_str(), auth_res.client_type.c_str(),
                         req.session_id);
        }

        // 4. Start keepalive timer for this session
        responses.push_back(make_start_keepalive_timer(req.session_id));
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

    LOG_INFO_MSG("[ACK] from=%s ts=%s status=%d sess=%s", msg.source_id, ack_for_timestamp.c_str(), status_code,
                 req.session_id);

    // Tell reactor to cancel the ACK timer
    responses.push_back(make_cancel_timer(req.session_id, ack_for_timestamp.c_str()));

    return responses;
}

// ==================== OTHER MESSAGES ====================

std::vector<response_slot_t> message_handler::handle_other_message(const message_t& msg, const request_slot_t& req,
                                                                   message_category category)
{
    std::vector<response_slot_t> responses;

    LOG_INFO_MSG("[MSG] type=%s from=%s ts=%s sess=%s", msg.msg_type, msg.source_id, msg.timestamp, req.session_id);

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
                std::cerr << "[MSG] dispatch create fail txn=" << fulfilled.transaction_id << '\n';
                LOG_ERROR_MSG("[MSG] dispatch create fail txn=%d", fulfilled.transaction_id);
                continue;
            }

            // Send to warehouse by username (reactor resolves session + starts ACK timer)
            responses.push_back(make_send_to_username(fulfilled.assigned_warehouse_id.c_str(), dispatch_msg));
            LOG_INFO_MSG("[MSG] dispatch -> wh=%s txn=%d", fulfilled.assigned_warehouse_id.c_str(),
                         fulfilled.transaction_id);
        }
    }
    else if (category == message_category::STOCK_REQ)
    {
        stock_request_result stock_result = m_inventory_manager.handle_stock_request(msg);

        if (!stock_result.success)
        {
            std::cerr << "[MSG] stock_request fail\n";
            LOG_ERROR_MSG("[MSG] stock_request fail from=%s", msg.source_id);
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
                std::cerr << "[MSG] dispatch create fail wh=" << stock_result.assigned_warehouse_id << '\n';
                LOG_ERROR_MSG("[MSG] dispatch create fail wh=%s", stock_result.assigned_warehouse_id.c_str());
                return responses;
            }

            responses.push_back(make_send_to_username(stock_result.assigned_warehouse_id.c_str(), dispatch_msg));
            LOG_INFO_MSG("[MSG] stock_req dispatch -> wh=%s hub=%s", stock_result.assigned_warehouse_id.c_str(),
                         stock_result.requesting_hub_id.c_str());
        }
    }
    else if (category == message_category::RECEIPT_CONFIRM)
    {
        m_inventory_manager.handle_receipt_confirmation(msg);
    }
    else if (category == message_category::DISPATCH_NOTICE)
    {
        stock_request_result shipment_result = m_inventory_manager.handle_shipment_notice(msg);

        if (shipment_result.success && !shipment_result.requesting_hub_id.empty())
        {
            // Notify the hub that stock is incoming
            message_t incoming_msg;
            int create_result = create_items_message(&incoming_msg, SERVER_TO_HUB__INCOMING_STOCK_NOTICE, SERVER,
                                                     shipment_result.requesting_hub_id.c_str(), shipment_result.items,
                                                     shipment_result.item_count, NULL);
            if (create_result == 0)
            {
                responses.push_back(make_send_to_username(shipment_result.requesting_hub_id.c_str(), incoming_msg));
                LOG_INFO_MSG("[MSG] incoming_stock -> hub=%s", shipment_result.requesting_hub_id.c_str());
            }
            else
            {
                std::cerr << "[MSG] incoming_stock create fail hub=" << shipment_result.requesting_hub_id << '\n';
                LOG_ERROR_MSG("[MSG] incoming_stock create fail hub=%s", shipment_result.requesting_hub_id.c_str());
            }
        }
    }
    else if (category == message_category::REPLENISH_REQ)
    {
        stock_request_result replenish_result = m_inventory_manager.handle_replenish_request(msg);

        if (!replenish_result.success)
        {
            std::cerr << "[MSG] replenish fail\n";
            LOG_ERROR_MSG("[MSG] replenish fail from=%s", msg.source_id);
            return responses;
        }

        message_t restock_msg;
        int create_result = create_items_message(&restock_msg, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE, SERVER,
                                                 replenish_result.assigned_warehouse_id.c_str(), replenish_result.items,
                                                 replenish_result.item_count, NULL);
        if (create_result != 0)
        {
            std::cerr << "[MSG] restock create fail\n";
            LOG_ERROR_MSG("[MSG] restock create fail wh=%s", replenish_result.assigned_warehouse_id.c_str());
            return responses;
        }

        // Restock goes back to the same session that sent the replenish request
        responses.push_back(make_send_response(req.session_id, restock_msg));
        responses.push_back(make_start_timer(req.session_id, restock_msg));
        LOG_INFO_MSG("[MSG] restock -> wh=%s sess=%s", replenish_result.assigned_warehouse_id.c_str(), req.session_id);
    }
    else if (category == message_category::EMERGENCY_ALERT)
    {
        // Build SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT using the client's emergency_type as instructions
        message_t broadcast_msg;
        if (create_server_emergency_message(&broadcast_msg, msg.payload.client_emergency.emergency_code,
                                            msg.payload.client_emergency.emergency_type) == 0)
        {
            response_slot_t slot;
            std::memset(&slot, 0, sizeof(slot));
            slot.command = static_cast<std::uint8_t>(response_command::BROADCAST);

            char json_buf[BUFFER_SIZE];
            if (serialize_message_to_json(&broadcast_msg, json_buf) == 0)
            {
                std::uint32_t len = static_cast<std::uint32_t>(std::strlen(json_buf));
                std::memcpy(slot.payload, json_buf, len);
                slot.payload_len = len;
                slot.start_ack_timer = true;
                std::strncpy(slot.timer_key, broadcast_msg.timestamp, TIMESTAMP_SIZE - 1);
                slot.timer_timeout = m_ack_timeout_seconds;
                slot.max_retries = m_max_retries;
                responses.push_back(slot);
            }
            LOG_INFO_MSG("[MSG] emergency broadcast from=%s code=%d", msg.source_id,
                         msg.payload.client_emergency.emergency_code);
        }
        else
        {
            LOG_ERROR_MSG("[MSG] emergency broadcast create fail from=%s", msg.source_id);
        }
    }
    else if (category == message_category::CLI_COMMAND)
    {
        if (!m_admin_handle)
        {
            LOG_WARNING_MSG("[MSG] CLI command but admin plugin not loaded, from=%s", msg.source_id);
            return responses;
        }

        // Forward the raw JSON to the plugin and send the response back
        char resp_buf[ADMIN_RESPONSE_MAX];
        if (m_admin_handle(req.raw_json, resp_buf, sizeof(resp_buf)) == 0)
        {
            response_slot_t slot;
            std::memset(&slot, 0, sizeof(slot));
            slot.command = static_cast<std::uint8_t>(response_command::SEND);
            std::strncpy(slot.session_id, req.session_id, SESSION_ID_SIZE - 1);

            std::uint32_t len = static_cast<std::uint32_t>(std::strlen(resp_buf));
            std::memcpy(slot.payload, resp_buf, len);
            slot.payload_len = len;
            responses.push_back(slot);
            LOG_INFO_MSG("[MSG] CLI response -> sess=%s len=%u", req.session_id, len);
        }
        else
        {
            LOG_ERROR_MSG("[MSG] admin_cli_handle failed from=%s", msg.source_id);
        }
    }
    else
    {
        LOG_WARNING_MSG("[MSG] unhandled type=%s from=%s", msg.msg_type, msg.source_id);
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

    if (std::strcmp(msg_type, HUB_TO_SERVER__KEEPALIVE) == 0 ||
        std::strcmp(msg_type, WAREHOUSE_TO_SERVER__KEEPALIVE) == 0)
    {
        return message_category::KEEPALIVE_MSG;
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

    if (std::strcmp(msg_type, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION) == 0 ||
        std::strcmp(msg_type, WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION) == 0)
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

    if (std::strcmp(msg_type, HUB_TO_SERVER__EMERGENCY_ALERT) == 0 ||
        std::strcmp(msg_type, WAREHOUSE_TO_SERVER__EMERGENCY_ALERT) == 0)
    {
        return message_category::EMERGENCY_ALERT;
    }

    if (std::strcmp(msg_type, CLI_TO_SERVER__ADMIN_COMMAND) == 0)
    {
        return message_category::CLI_COMMAND;
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
