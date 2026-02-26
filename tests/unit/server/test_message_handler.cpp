#include <asio.hpp>
#include <common/json_manager.h>
#include <gtest/gtest.h>
#include <server/auth_module.hpp>
#include <server/database.hpp>
#include <server/inventory_manager.hpp>
#include <server/message_handler.hpp>
#include <server/session_manager.hpp>
#include <server/timer_manager.hpp>

class MessageHandlerTest : public ::testing::Test
{
  protected:
    asio::io_context io_context;
    std::unique_ptr<pqxx::connection> db_conn;
    std::unique_ptr<auth_module> auth_mod;
    std::unique_ptr<session_manager> session_mgr;
    std::unique_ptr<timer_manager> timer_mgr;
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

        auth_mod = std::make_unique<auth_module>(*db_conn);
        session_mgr = std::make_unique<session_manager>();
        timer_mgr = std::make_unique<timer_manager>(io_context);
        inv_mgr = std::make_unique<inventory_manager>(*db_conn);
        // Dummy send callback for tests
        auto send_callback = [](const std::string& session_id, const std::string& data) {
            std::cout << "[TEST] Send callback called for session: " << session_id << std::endl;
        };
        msg_handler = std::make_unique<message_handler>(*auth_mod, *session_mgr, *timer_mgr, *inv_mgr, send_callback);
    }

    void TearDown() override
    {
        msg_handler.reset();
        inv_mgr.reset();
        session_mgr.reset();
        auth_mod.reset();
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
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    char json_input[1024];
    serialize_message_to_json(&auth_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.should_send_response);
    EXPECT_EQ(result.response_message.payload.server_auth_response.status_code, 200);
    EXPECT_TRUE(session_mgr->is_authenticated(session_id));
}

TEST_F(MessageHandlerTest, ProcessAuthRequestInvalidCredentials)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "wrong_hash");

    char json_input[1024];
    serialize_message_to_json(&auth_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.should_send_response);
    EXPECT_EQ(result.response_message.payload.server_auth_response.status_code, 401);
    EXPECT_FALSE(session_mgr->is_authenticated(session_id));
}

TEST_F(MessageHandlerTest, ProcessAuthRequestWarehouseUser)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "WAREHOUSE", "warehouse_user", "warehouse_user", "warehouse_hash");

    char json_input[1024];
    serialize_message_to_json(&auth_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.should_send_response);
    EXPECT_EQ(result.response_message.payload.server_auth_response.status_code, 200);
    EXPECT_STREQ(result.response_message.msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE);
}

TEST_F(MessageHandlerTest, ProcessInvalidJsonInput)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    std::string invalid_json = "{invalid json}";

    message_processing_result result = msg_handler->process_message(invalid_json, session_id);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(MessageHandlerTest, ProcessEmptyJsonInput)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    std::string empty_json = "";

    message_processing_result result = msg_handler->process_message(empty_json, session_id);

    EXPECT_FALSE(result.success);
}

TEST_F(MessageHandlerTest, ProcessMessageWithoutAuthentication)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t status_msg;
    create_keepalive_message(&status_msg, "HUB", "hub_001", "K");

    char json_input[1024];
    serialize_message_to_json(&status_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(MessageHandlerTest, ProcessMessageAfterAuthentication)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    char auth_json[1024];
    serialize_message_to_json(&auth_msg, auth_json);

    message_processing_result auth_result = msg_handler->process_message(auth_json, session_id);
    EXPECT_TRUE(auth_result.success);
    EXPECT_TRUE(session_mgr->is_authenticated(session_id));

    message_t status_msg;
    create_keepalive_message(&status_msg, "HUB", "hub_user", "K");

    char status_json[1024];
    serialize_message_to_json(&status_msg, status_json);

    message_processing_result status_result = msg_handler->process_message(status_json, session_id);

    EXPECT_TRUE(status_result.success);
}

// ==================== ACK GENERATION TESTS ====================

TEST_F(MessageHandlerTest, GenerateAckForAuthenticatedKeepalive)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_user");

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_user", "K");

    message_t ack_msg;
    bool ack_generated = msg_handler->generate_ack_if_needed(keepalive_msg, session_id, &ack_msg);

    EXPECT_TRUE(ack_generated);
    EXPECT_STREQ(ack_msg.msg_type, SERVER_TO_HUB__ACK);
    EXPECT_STREQ(ack_msg.source_role, SERVER);
    EXPECT_STREQ(ack_msg.target_role, "HUB");
    EXPECT_STREQ(ack_msg.target_id, "hub_user");
    EXPECT_EQ(ack_msg.payload.acknowledgment.status_code, 200);
    EXPECT_STREQ(ack_msg.payload.acknowledgment.ack_for_timestamp, keepalive_msg.timestamp);
}

TEST_F(MessageHandlerTest, GenerateAckForAuthenticatedInventoryUpdate)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "WAREHOUSE", "warehouse_001");

    inventory_item_t items[1] = {{1, "Test Item", 10}};
    message_t inventory_msg;
    create_items_message(&inventory_msg, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, "warehouse_001", SERVER, items, 1);

    message_t ack_msg;
    bool ack_generated = msg_handler->generate_ack_if_needed(inventory_msg, session_id, &ack_msg);

    EXPECT_TRUE(ack_generated);
    EXPECT_STREQ(ack_msg.msg_type, SERVER_TO_WAREHOUSE__ACK);
    EXPECT_STREQ(ack_msg.payload.acknowledgment.ack_for_timestamp, inventory_msg.timestamp);
}

TEST_F(MessageHandlerTest, NoAckForAuthRequest)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "hub_user", "hub_user", "hub_hash");

    message_t ack_msg;
    bool ack_generated = msg_handler->generate_ack_if_needed(auth_msg, session_id, &ack_msg);

    EXPECT_FALSE(ack_generated);
}

TEST_F(MessageHandlerTest, NoAckForAckMessage)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_user");

    message_t ack_msg_in;
    create_acknowledgment_message(&ack_msg_in, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    message_t ack_msg_out;
    bool ack_generated = msg_handler->generate_ack_if_needed(ack_msg_in, session_id, &ack_msg_out);

    EXPECT_FALSE(ack_generated);
}

TEST_F(MessageHandlerTest, NoAckForUnauthenticatedSession)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    // Session NOT authenticated

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_001", "K");

    message_t ack_msg;
    bool ack_generated = msg_handler->generate_ack_if_needed(keepalive_msg, session_id, &ack_msg);

    EXPECT_FALSE(ack_generated);
}

// ==================== ACK PROCESSING TESTS ====================

TEST_F(MessageHandlerTest, ProcessAckFromAuthenticatedClient)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_user");

    message_t ack_msg;
    create_acknowledgment_message(&ack_msg, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    char json_input[1024];
    serialize_message_to_json(&ack_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.should_send_response); // ACKs don't get responses
}

TEST_F(MessageHandlerTest, RejectAckFromUnauthenticatedClient)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    // Session NOT authenticated

    message_t ack_msg;
    create_acknowledgment_message(&ack_msg, "HUB", "hub_user", SERVER, SERVER, "2025-12-10T10:00:00Z", 200);

    char json_input[1024];
    serialize_message_to_json(&ack_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

// ==================== SECURITY TESTS ====================

TEST_F(MessageHandlerTest, RejectUnauthenticatedInventoryUpdate)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    // Session NOT authenticated

    inventory_item_t items[1] = {{1, "Test Item", 10}};
    message_t inventory_msg;
    create_items_message(&inventory_msg, HUB_TO_SERVER__INVENTORY_UPDATE, "hub_001", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&inventory_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
    EXPECT_NE(result.error_message.find("not authenticated"), std::string::npos);
}

TEST_F(MessageHandlerTest, AcceptAuthenticatedInventoryUpdate)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_user");

    inventory_item_t items[1] = {{1, "Test Item", 10}};
    message_t inventory_msg;
    create_items_message(&inventory_msg, HUB_TO_SERVER__INVENTORY_UPDATE, "hub_user", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&inventory_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

TEST_F(MessageHandlerTest, ProcessStockRequestHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_1");

    inventory_item_t items[1] = {{1, "Food", 5}};
    message_t stock_msg;
    create_items_message(&stock_msg, HUB_TO_SERVER__STOCK_REQUEST, "hub_1", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&stock_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
    // Even if no warehouse is present, it should be queued as PENDING
}

TEST_F(MessageHandlerTest, ProcessReplenishRequestWarehouseToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "WAREHOUSE", "warehouse_1");

    inventory_item_t items[1] = {{1, "Food", 100}};
    message_t replenish_msg;
    create_items_message(&replenish_msg, WAREHOUSE_TO_SERVER__REPLENISH_REQUEST, "warehouse_1", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&replenish_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

TEST_F(MessageHandlerTest, ProcessShipmentNoticeWarehouseToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    // Setup an ASSIGNED transaction first
    {
        pqxx::work txn(*db_conn);
        txn.exec("INSERT INTO inventory_transactions (transaction_type, source_id, source_type, destination_id, "
                 "destination_type, status, food) "
                 "VALUES ('STOCK_REQUEST', 'warehouse_1', 'WAREHOUSE', 'hub_1', 'HUB', 'ASSIGNED', 10)");
        txn.commit();
    }

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "WAREHOUSE", "warehouse_1");

    inventory_item_t items[1] = {{1, "Food", 10}};
    message_t shipment_msg;
    create_items_message(&shipment_msg, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, "warehouse_1", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&shipment_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}
TEST_F(MessageHandlerTest, InventoryUpdateTriggersPendingFulfillment)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    // 1. Create a pending request from hub_1
    {
        pqxx::work txn(*db_conn);
        txn.exec(
            "INSERT INTO inventory_transactions (transaction_type, destination_id, destination_type, status, food) "
            "VALUES ('STOCK_REQUEST', 'hub_1', 'HUB', 'PENDING', 10)");
        txn.commit();
    }

    // 2. Setup warehouse session
    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "WAREHOUSE", "warehouse_1");

    // 3. Send INV_UPDATE from warehouse that has stock
    inventory_item_t items[1] = {{1, "Food", 100}};
    message_t update_msg;
    create_items_message(&update_msg, WAREHOUSE_TO_SERVER__INVENTORY_UPDATE, "warehouse_1", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&update_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);

    // Verify transaction became ASSIGNED
    pqxx::work verify_txn(*db_conn);
    pqxx::result db_res =
        verify_txn.exec("SELECT status, source_id FROM inventory_transactions WHERE destination_id = 'hub_1'");
    EXPECT_EQ(db_res[0][0].as<std::string>(), "ASSIGNED");
    EXPECT_EQ(db_res[0][1].as<std::string>(), "warehouse_1");
}

TEST_F(MessageHandlerTest, ProcessEmergencyAlertHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_1");

    message_t alert_msg;
    create_client_emergency_message(&alert_msg, "HUB", "hub_1", 1, "FIRE");

    char json_input[1024];
    serialize_message_to_json(&alert_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

TEST_F(MessageHandlerTest, ProcessKeepaliveHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_1");

    message_t keepalive_msg;
    create_keepalive_message(&keepalive_msg, "HUB", "hub_1", "I am alive");

    char json_input[1024];
    serialize_message_to_json(&keepalive_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

TEST_F(MessageHandlerTest, ProcessReceiptConfirmationHubToServer)
{
    if (!db_conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*db_conn);

    std::string session_id = session_mgr->create_session();
    session_mgr->mark_authenticated(session_id, "HUB", "hub_1");

    inventory_item_t items[1] = {{1, "Food", 10}};
    message_t receipt_msg;
    create_items_message(&receipt_msg, HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION, "hub_1", SERVER, items, 1);

    char json_input[1024];
    serialize_message_to_json(&receipt_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
