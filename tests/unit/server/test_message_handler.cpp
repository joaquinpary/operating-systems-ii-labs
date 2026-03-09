#include <common/json_manager.h>
#include <gtest/gtest.h>
#include <server/auth_module.hpp>
#include <server/connection_pool.hpp>
#include <server/database.hpp>
#include <server/inventory_manager.hpp>
#include <server/ipc.hpp>
#include <server/message_handler.hpp>

// Helper: fill a request_slot_t from a session_id + JSON + auth state
static request_slot_t make_request(const char* session_id, const char* json, bool is_authenticated = false,
                                   bool is_blacklisted = false, const char* client_type = "",
                                   const char* username = "")
{
    request_slot_t req;
    std::memset(&req, 0, sizeof(req));
    std::strncpy(req.session_id, session_id, SESSION_ID_SIZE - 1);
    std::strncpy(req.raw_json, json, BUFFER_SIZE - 1);
    req.payload_len = static_cast<std::uint32_t>(std::strlen(json));
    req.is_authenticated = is_authenticated;
    req.is_blacklisted = is_blacklisted;
    req.protocol = static_cast<std::uint8_t>(protocol_type::TCP);
    std::strncpy(req.client_type, client_type, ROLE_SIZE - 1);
    std::strncpy(req.username, username, CREDENTIALS_SIZE - 1);
    return req;
}

// Helper: find a response of a given command type in a vector
static const response_slot_t* find_response(const std::vector<response_slot_t>& responses, response_command cmd)
{
    for (const auto& r : responses)
    {
        if (static_cast<response_command>(r.command) == cmd)
        {
            return &r;
        }
    }
    return nullptr;
}

class MessageHandlerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<pqxx::connection> db_conn;
    std::shared_ptr<connection_pool> db_pool;
    std::unique_ptr<auth_module> auth_mod;
    std::unique_ptr<inventory_manager> inv_mgr;
    std::unique_ptr<message_handler> msg_handler;

    void SetUp() override
    {
        db_conn = connect_to_database();
        if (!db_conn)
        {
            GTEST_SKIP() << "Database not available, skipping message_handler tests";
        }

        create_credentials_table(*db_conn);

        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM credentials");
            txn.commit();
        }

        {
            pqxx::work txn(*db_conn);
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('hub_user', 'hub_hash', 'HUB', TRUE)");
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('warehouse_user', 'warehouse_hash', 'WAREHOUSE', TRUE)");
            txn.commit();
        }

        db_pool = std::make_shared<connection_pool>(build_connection_string(), 1);
        auth_mod = std::make_unique<auth_module>(*db_pool);
        inv_mgr = std::make_unique<inventory_manager>(*db_pool);
        msg_handler = std::make_unique<message_handler>(*auth_mod, *inv_mgr, 5, 3);
    }

    void TearDown() override
    {
        msg_handler.reset();
        inv_mgr.reset();
        auth_mod.reset();
        db_pool.reset();
        if (db_conn)
        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM credentials WHERE username LIKE '%_user'");
            txn.commit();
        }
        db_conn.reset();
    }
};

TEST_F(MessageHandlerTest, ProcessAuthRequestSuccess)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&auth_msg, json_input);

    auto req = make_request("tcp_session_1", json_input);
    auto responses = msg_handler->process_request(req);

    // Expect: MARK_AUTHENTICATED, SEND (auth response), START_ACK_TIMER, SEND (inventory), START_ACK_TIMER
    auto* mark_auth = find_response(responses, response_command::MARK_AUTHENTICATED);
    ASSERT_NE(mark_auth, nullptr);
    EXPECT_STREQ(mark_auth->client_type, "HUB");
    EXPECT_STREQ(mark_auth->username, "hub_user");

    // At least one SEND response
    auto* send = find_response(responses, response_command::SEND);
    ASSERT_NE(send, nullptr);
    EXPECT_GT(send->payload_len, 0u);

    // Deserialize the sent auth response
    message_t sent_msg;
    std::string payload(send->payload, send->payload_len);
    ASSERT_EQ(deserialize_message_from_json(payload.c_str(), &sent_msg), 0);
    EXPECT_EQ(sent_msg.payload.server_auth_response.status_code, 200);
}

TEST_F(MessageHandlerTest, ProcessAuthRequestInvalidCredentials)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "wrong_hash");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&auth_msg, json_input);

    auto req = make_request("tcp_session_1", json_input);
    auto responses = msg_handler->process_request(req);

    // Should get a SEND with 401 status, no MARK_AUTHENTICATED
    auto* mark_auth = find_response(responses, response_command::MARK_AUTHENTICATED);
    EXPECT_EQ(mark_auth, nullptr);

    auto* send = find_response(responses, response_command::SEND);
    ASSERT_NE(send, nullptr);

    message_t sent_msg;
    std::string payload(send->payload, send->payload_len);
    ASSERT_EQ(deserialize_message_from_json(payload.c_str(), &sent_msg), 0);
    EXPECT_EQ(sent_msg.payload.server_auth_response.status_code, 401);
}

TEST_F(MessageHandlerTest, ProcessAuthRequestWarehouseUser)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "WAREHOUSE", "warehouse_user", "warehouse_user", "warehouse_hash");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&auth_msg, json_input);

    auto req = make_request("tcp_session_1", json_input);
    auto responses = msg_handler->process_request(req);

    auto* mark_auth = find_response(responses, response_command::MARK_AUTHENTICATED);
    ASSERT_NE(mark_auth, nullptr);
    EXPECT_STREQ(mark_auth->client_type, "WAREHOUSE");
    EXPECT_STREQ(mark_auth->username, "warehouse_user");
}

TEST_F(MessageHandlerTest, ProcessInvalidJsonInput)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    auto req = make_request("tcp_session_1", "{invalid json}");
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessEmptyJsonInput)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    auto req = make_request("tcp_session_1", "");
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessMessageWithoutAuthentication)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t status_msg;
    create_keepalive_message(&status_msg, "HUB", "hub_001", "K");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&status_msg, json_input);

    // Not authenticated
    auto req = make_request("tcp_session_1", json_input, false);
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessMessageAfterAuthentication)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t status_msg;
    create_keepalive_message(&status_msg, "HUB", "hub_user", "K");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&status_msg, json_input);

    // Simulate authenticated session
    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    // Should get an ACK response (SEND)
    auto* send = find_response(responses, response_command::SEND);
    ASSERT_NE(send, nullptr);
}

// ==================== ACK GENERATION TESTS ====================

TEST_F(MessageHandlerTest, GenerateAckForAuthenticatedKeepalive)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_user", "K");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&keepalive_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    // First response should be the ACK
    ASSERT_FALSE(responses.empty());
    auto& ack = responses[0];
    EXPECT_EQ(static_cast<response_command>(ack.command), response_command::SEND);

    message_t ack_msg;
    std::string payload(ack.payload, ack.payload_len);
    ASSERT_EQ(deserialize_message_from_json(payload.c_str(), &ack_msg), 0);
    EXPECT_STREQ(ack_msg.msg_type, SERVER_TO_HUB__ACK);
    EXPECT_EQ(ack_msg.payload.acknowledgment.status_code, 200);
    EXPECT_STREQ(ack_msg.payload.acknowledgment.ack_for_timestamp, keepalive_msg.timestamp);
}

TEST_F(MessageHandlerTest, NoAckForAuthRequest)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&auth_msg, json_input);

    auto req = make_request("tcp_session_1", json_input);
    auto responses = msg_handler->process_request(req);

    // No ACK for auth requests — the first response should be MARK_AUTHENTICATED or SEND(auth_response)
    for (const auto& r : responses)
    {
        if (static_cast<response_command>(r.command) == response_command::SEND)
        {
            message_t msg;
            std::string payload(r.payload, r.payload_len);
            if (deserialize_message_from_json(payload.c_str(), &msg) == 0)
            {
                // The SEND should be an auth response, not an ACK
                EXPECT_STRNE(msg.msg_type, SERVER_TO_HUB__ACK);
            }
        }
    }
}

TEST_F(MessageHandlerTest, NoAckForAckMessage)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t ack_msg_in;
    create_acknowledgment_message(&ack_msg_in, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&ack_msg_in, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    // Should get only a CANCEL_ACK_TIMER, no ACK SEND
    for (const auto& r : responses)
    {
        if (static_cast<response_command>(r.command) == response_command::SEND)
        {
            FAIL() << "ACK messages should not generate a SEND response";
        }
    }
    auto* cancel = find_response(responses, response_command::CANCEL_ACK_TIMER);
    ASSERT_NE(cancel, nullptr);
}

TEST_F(MessageHandlerTest, NoAckForUnauthenticatedSession)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_001", "K");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&keepalive_msg, json_input);

    // Not authenticated — should reject entirely
    auto req = make_request("tcp_session_1", json_input, false);
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

// ==================== ACK PROCESSING TESTS ====================

TEST_F(MessageHandlerTest, ProcessAckFromAuthenticatedClient)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t ack_msg;
    create_acknowledgment_message(&ack_msg, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&ack_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    auto* cancel = find_response(responses, response_command::CANCEL_ACK_TIMER);
    ASSERT_NE(cancel, nullptr);
    EXPECT_STREQ(cancel->timer_key, "2025-12-10T10:00:00Z");
}

TEST_F(MessageHandlerTest, RejectAckFromUnauthenticatedClient)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t ack_msg;
    create_acknowledgment_message(&ack_msg, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&ack_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, false);
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

// ==================== SECURITY TESTS ====================

TEST_F(MessageHandlerTest, RejectUnauthenticatedInventoryUpdate)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    inventory_item_t items[1] = {{1, "Test Item", 10}};
    message_t inventory_msg;
    create_items_message(&inventory_msg, HUB_TO_SERVER__INVENTORY_UPDATE, "hub_001", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&inventory_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, false);
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

TEST_F(MessageHandlerTest, AcceptAuthenticatedInventoryUpdate)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    inventory_item_t items[1] = {{1, "Test Item", 10}};
    message_t inventory_msg;
    create_items_message(&inventory_msg, HUB_TO_SERVER__INVENTORY_UPDATE, "hub_user", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&inventory_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    // Should at least have an ACK SEND
    EXPECT_FALSE(responses.empty());
}

TEST_F(MessageHandlerTest, RejectBlacklistedSession)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&auth_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, true, "HUB", "hub_user");
    auto responses = msg_handler->process_request(req);

    EXPECT_TRUE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessStockRequestHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    inventory_item_t items[1] = {{1, "Food", 5}};
    message_t stock_msg;
    create_items_message(&stock_msg, HUB_TO_SERVER__STOCK_REQUEST, "hub_1", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&stock_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_1");
    auto responses = msg_handler->process_request(req);

    // Should at least have an ACK
    EXPECT_FALSE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessReplenishRequestWarehouseToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    inventory_item_t items[1] = {{1, "Food", 100}};
    message_t replenish_msg;
    create_items_message(&replenish_msg, WAREHOUSE_TO_SERVER__REPLENISH_REQUEST, "warehouse_1", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&replenish_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "WAREHOUSE", "warehouse_1");
    auto responses = msg_handler->process_request(req);

    EXPECT_FALSE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessShipmentNoticeWarehouseToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    {
        pqxx::work txn(*db_conn);
        txn.exec("INSERT INTO inventory_transactions (transaction_type, source_id, source_type, destination_id, "
                 "destination_type, status, food) "
                 "VALUES ('STOCK_REQUEST', 'warehouse_1', 'WAREHOUSE', 'hub_1', 'HUB', 'ASSIGNED', 10)");
        txn.commit();
    }

    inventory_item_t items[1] = {{1, "Food", 10}};
    message_t shipment_msg;
    create_items_message(&shipment_msg, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, "warehouse_1", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&shipment_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "WAREHOUSE", "warehouse_1");
    auto responses = msg_handler->process_request(req);

    // Should at least have an ACK
    EXPECT_FALSE(responses.empty());
}

TEST_F(MessageHandlerTest, InventoryUpdateTriggersPendingFulfillment)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    {
        pqxx::work txn(*db_conn);
        txn.exec(
            "INSERT INTO inventory_transactions (transaction_type, destination_id, destination_type, status, food) "
            "VALUES ('STOCK_REQUEST', 'hub_1', 'HUB', 'PENDING', 10)");
        txn.commit();
    }

    inventory_item_t items[1] = {{1, "Food", 100}};
    message_t update_msg;
    create_items_message(&update_msg, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, "warehouse_1", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&update_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "WAREHOUSE", "warehouse_1");
    auto responses = msg_handler->process_request(req);

    EXPECT_FALSE(responses.empty());

    // Verify transaction became ASSIGNED in DB
    pqxx::work verify_txn(*db_conn);
    pqxx::result db_res =
        verify_txn.exec("SELECT status, source_id FROM inventory_transactions WHERE destination_id = 'hub_1'");
    EXPECT_EQ(db_res[0][0].as<std::string>(), "ASSIGNED");
    EXPECT_EQ(db_res[0][1].as<std::string>(), "warehouse_1");
}

TEST_F(MessageHandlerTest, ProcessKeepaliveHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_1", "I am alive");

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&keepalive_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_1");
    auto responses = msg_handler->process_request(req);

    // Should get an ACK
    EXPECT_FALSE(responses.empty());
}

TEST_F(MessageHandlerTest, ProcessReceiptConfirmationHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    inventory_item_t items[1] = {{1, "Food", 10}};
    message_t receipt_msg;
    create_items_message(&receipt_msg, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, "hub_1", SERVER, items, 1, NULL);

    char json_input[BUFFER_SIZE];
    serialize_message_to_json(&receipt_msg, json_input);

    auto req = make_request("tcp_session_1", json_input, true, false, "HUB", "hub_1");
    auto responses = msg_handler->process_request(req);

    EXPECT_FALSE(responses.empty());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
