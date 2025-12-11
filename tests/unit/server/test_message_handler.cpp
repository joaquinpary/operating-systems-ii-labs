#include <gtest/gtest.h>
#include <server/message_handler.hpp>
#include <server/auth_module.hpp>
#include <server/session_manager.hpp>
#include <server/timer_manager.hpp>
#include <server/database.hpp>
#include <common/json_manager.h>
#include <asio.hpp>

class MessageHandlerTest : public ::testing::Test
{
  protected:
    asio::io_context io_context;
    std::unique_ptr<pqxx::connection> db_conn;
    std::unique_ptr<auth_module> auth_mod;
    std::unique_ptr<session_manager> session_mgr;
    std::unique_ptr<timer_manager> timer_mgr;
    std::unique_ptr<message_handler> msg_handler;

    void SetUp() override
    {
        db_conn = connect_to_database();
        if (!db_conn)
        {
            GTEST_SKIP() << "Database not available, skipping message_handler tests";
        }

        {
            pqxx::work txn(*db_conn);
            create_credentials_table(txn);
        }

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
        // Dummy send callback for tests
        auto send_callback = [](const std::string& session_id, const std::string& data) {
            std::cout << "[TEST] Send callback called for session: " << session_id << std::endl;
        };
        msg_handler = std::make_unique<message_handler>(*auth_mod, *session_mgr, *timer_mgr, send_callback);
    }

    void TearDown() override
    {
        msg_handler.reset();
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
    create_keepalive_message(&status_msg, "HUB", "hub_001", 'K');

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
    create_keepalive_message(&status_msg, "HUB", "hub_user", 'K');

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
    create_keepalive_message(&keepalive_msg, "HUB", "hub_user", 'K');

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
    create_items_message(&inventory_msg, "WAREHOUSE", "warehouse_001", SERVER, SERVER, INVENTORY_UPDATE, items, 1);

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
    create_keepalive_message(&keepalive_msg, "HUB", "hub_001", 'K');

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
    create_items_message(&inventory_msg, "HUB", "hub_001", SERVER, SERVER, INVENTORY_UPDATE, items, 1);

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
    create_items_message(&inventory_msg, "HUB", "hub_user", SERVER, SERVER, INVENTORY_UPDATE, items, 1);

    char json_input[1024];
    serialize_message_to_json(&inventory_msg, json_input);

    message_processing_result result = msg_handler->process_message(json_input, session_id);

    EXPECT_TRUE(result.success);
}

