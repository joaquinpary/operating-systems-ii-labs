#include "auth_module.hpp"
#include "connection_pool.hpp"
#include "database.hpp"
#include "inventory_manager.hpp"
#include "message_handler.hpp"
#include "server.hpp"
#include "session_manager.hpp"
#include "timer_manager.hpp"
#include <asio.hpp>
#include <chrono>
#include <common/json_manager.h>
#include <gtest/gtest.h>
#include <memory>
#include <pqxx/pqxx>
#include <thread>

using namespace asio::ip;

namespace
{
config::server_config make_test_server_config()
{
    return config::server_config{
        .ip_v4 = "127.0.0.1",
        .ip_v6 = "::1",
        .network_port = 9999,
        .ack_timeout = 3,
        .max_auth_attempts = 3,
        .max_retries = 3,
        .pool_size = 1,
        .credentials_path = "config/clients",
    };
}
} // namespace

class ServerTest : public ::testing::Test
{
  protected:
    static std::atomic<uint16_t> s_next_port;

    uint16_t get_unique_port()
    {
        return s_next_port.fetch_add(1);
    }

    void SetUp() override
    {
        m_io_context = std::make_unique<asio::io_context>();

        // Create core modules for testing
        // Note: For tests, we'll try to use real DB connection if available,
        // otherwise tests will need to be skipped
        auto db_connection = connect_to_database();
        if (db_connection)
        {
            m_db_pool = std::make_shared<connection_pool>(build_connection_string(), 1);
            m_session_mgr = std::make_unique<session_manager>();
            m_auth_mod = std::make_unique<auth_module>(*m_db_pool);
            m_timer_mgr = std::make_unique<timer_manager>(*m_io_context);
            m_inv_mgr = std::make_unique<inventory_manager>(*m_db_pool);
            // Dummy send callback for tests
            auto send_callback = [](const std::string&, const std::string&) {};
            m_msg_handler = std::make_unique<message_handler>(*m_auth_mod, *m_session_mgr, *m_timer_mgr, *m_inv_mgr,
                                                              3, 3, send_callback);
            m_db_connection = std::move(db_connection);
        }
    }

    void TearDown() override
    {
        if (m_server)
        {
            m_server->stop();
        }
        m_io_context->stop();
        if (m_io_thread && m_io_thread->joinable())
        {
            m_io_thread->join();
        }
        m_server.reset();
        m_msg_handler.reset();
        m_inv_mgr.reset();
        m_auth_mod.reset();
        m_session_mgr.reset();
        m_db_pool.reset();
        m_db_connection.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void start_io_context()
    {
        m_io_thread = std::make_unique<std::thread>([this]() { m_io_context->run(); });
    }

    std::unique_ptr<asio::io_context> m_io_context;
    std::unique_ptr<server> m_server;
    std::unique_ptr<std::thread> m_io_thread;
    std::unique_ptr<pqxx::connection> m_db_connection;
    std::shared_ptr<connection_pool> m_db_pool;
    std::unique_ptr<session_manager> m_session_mgr;
    std::unique_ptr<auth_module> m_auth_mod;
    std::unique_ptr<timer_manager> m_timer_mgr;
    std::unique_ptr<inventory_manager> m_inv_mgr;
    std::unique_ptr<message_handler> m_msg_handler;
};

std::atomic<uint16_t> ServerTest::s_next_port{10000};

// Test default configuration values
TEST_F(ServerTest, DefaultConfigValues)
{
    config::server_config config = make_test_server_config();

    EXPECT_EQ(config.ip_v4, "127.0.0.1");
    EXPECT_EQ(config.ip_v6, "::1");
    EXPECT_EQ(config.network_port, 9999);
}

// Test server initialization with default configuration
TEST_F(ServerTest, ServerInitialization)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping server initialization test";
    }

    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    ASSERT_NO_THROW({
        m_server = std::make_unique<server>(
            *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
            std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    });
    ASSERT_NE(m_server, nullptr);
}

// Test server can start
TEST_F(ServerTest, ServerStart)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping server start test";
    }

    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    ASSERT_NO_THROW(m_server->start());
    start_io_context();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test server can stop
TEST_F(ServerTest, ServerStop)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping server stop test";
    }

    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    m_server->start();
    start_io_context();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NO_THROW(m_server->stop());
}

// Test basic TCP IPv4 connection
TEST_F(ServerTest, TCPIPv4Connection)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping TCP connection test";
    }

    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), test_port), ec);

    EXPECT_FALSE(ec);
    if (!ec)
    {
        client.close();
    }
}

// Test basic TCP IPv6 connection
TEST_F(ServerTest, TCPIPv6Connection)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping TCP IPv6 connection test";
    }

    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("::1"), test_port), ec);

    EXPECT_FALSE(ec);
    if (!ec)
    {
        client.close();
    }
}

// Test server processes messages (echo test removed - now requires authentication)
TEST_F(ServerTest, TCPMessageProcessing)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping message processing test";
    }

    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), test_port), ec);
    ASSERT_FALSE(ec);

    // Connection successful - message processing now requires authentication
    // This test just verifies the connection works
    client.close();
}

// Test UDP server processes authentication messages
TEST_F(ServerTest, UDPEchoResponse)
{
    if (!m_session_mgr || !m_auth_mod || !m_msg_handler)
    {
        GTEST_SKIP() << "Database not available, skipping UDP test";
    }

    {
        pqxx::work txn(*m_db_connection);
        txn.exec("DELETE FROM credentials WHERE username = 'udp_test_user'");
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                 "VALUES ('udp_test_user', 'test_hash', 'HUB', TRUE)");
        txn.commit();
    }

    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(
        *m_io_context, config, std::make_unique<session_manager>(), std::make_unique<auth_module>(*m_db_pool),
        std::make_unique<timer_manager>(*m_io_context), std::make_unique<inventory_manager>(*m_db_pool));
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    udp::socket client(*m_io_context, udp::endpoint(udp::v4(), 0));
    udp::endpoint server_endpoint(asio::ip::make_address("127.0.0.1"), test_port);

    message_t auth_request;
    create_auth_request_message(&auth_request, "HUB", "udp_test_user", "udp_test_user", "test_hash");

    char json_buffer[1024];
    int result = serialize_message_to_json(&auth_request, json_buffer);
    ASSERT_EQ(result, 0);

    std::string test_message(json_buffer);
    std::size_t bytes_sent = client.send_to(asio::buffer(test_message), server_endpoint);
    EXPECT_GT(bytes_sent, 0);

    // After successful auth, server sends both auth response and inventory update
    // Read both messages and verify auth response
    bool found_auth_response = false;
    for (int i = 0; i < 2; i++)
    {
        std::array<char, 1024> response;
        udp::endpoint sender_endpoint;
        asio::error_code ec;
        std::size_t bytes_received = client.receive_from(asio::buffer(response), sender_endpoint, 0, ec);
        EXPECT_GT(bytes_received, 0);
        EXPECT_FALSE(ec);

        message_t response_msg;
        result = deserialize_message_from_json(std::string(response.data(), bytes_received).c_str(), &response_msg);
        EXPECT_EQ(result, 0);

        if (strcmp(response_msg.msg_type, SERVER_TO_HUB__AUTH_RESPONSE) == 0)
        {
            EXPECT_EQ(response_msg.payload.server_auth_response.status_code, 200);
            found_auth_response = true;
        }
    }
    EXPECT_TRUE(found_auth_response);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
