#include <gtest/gtest.h>
#include <server/message_handler.hpp>
#include <server/auth_module.hpp>
#include <server/session_manager.hpp>
#include <server/database.hpp>
#include <common/json_manager.h>

class MessageHandlerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<pqxx::connection> db_conn;
    std::unique_ptr<auth_module> auth_mod;
    std::unique_ptr<session_manager> session_mgr;
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
        msg_handler = std::make_unique<message_handler>(*auth_mod, *session_mgr);
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
    create_status_message(&status_msg, "HUB", "hub_001", "SERVER", "server", "STATUS", 1);

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
    create_status_message(&status_msg, "HUB", "hub_user", "SERVER", "server", "STATUS", 1);

    char status_json[1024];
    serialize_message_to_json(&status_msg, status_json);

    message_processing_result status_result = msg_handler->process_message(status_json, session_id);

    EXPECT_TRUE(status_result.success);
}

