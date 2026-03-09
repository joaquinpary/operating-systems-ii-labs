#include "connection_pool.hpp"
#include "database.hpp"
#include "event_loop.hpp"
#include "ipc.hpp"
#include "server.hpp"
#include "session_manager.hpp"
#include "timer_manager.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <common/json_manager.h>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <netinet/in.h>
#include <pqxx/pqxx>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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
        .worker_threads = 2,
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
        auto db_connection = connect_to_database();
        if (db_connection)
        {
            m_db_pool = std::make_shared<connection_pool>(build_connection_string(), 1);
            m_db_connection = std::move(db_connection);
        }

        // Create shared memory and eventfd for each test
        m_shm = std::make_unique<shared_queue>(shared_queue::create());
        m_response_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        m_loop = std::make_unique<event_loop>();
    }

    void TearDown() override
    {
        if (m_server)
        {
            m_server->stop();
        }
        if (m_loop)
        {
            m_loop->stop();
        }
        if (m_io_thread && m_io_thread->joinable())
        {
            m_io_thread->join();
        }
        m_server.reset();
        m_db_pool.reset();
        m_db_connection.reset();
        m_loop.reset();
        if (m_response_efd >= 0)
        {
            close(m_response_efd);
            m_response_efd = -1;
        }
        m_shm.reset();
        shared_queue::unlink();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void start_loop()
    {
        m_io_thread = std::make_unique<std::thread>([this]() { m_loop->run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::unique_ptr<event_loop> m_loop;
    std::unique_ptr<shared_queue> m_shm;
    int m_response_efd = -1;
    std::unique_ptr<server> m_server;
    std::unique_ptr<std::thread> m_io_thread;
    std::unique_ptr<pqxx::connection> m_db_connection;
    std::shared_ptr<connection_pool> m_db_pool;
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
    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    ASSERT_NO_THROW({
        m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                            std::make_unique<session_manager>(),
                                            std::make_unique<timer_manager>(*m_loop));
    });
    ASSERT_NE(m_server, nullptr);
}

// Test server can start
TEST_F(ServerTest, ServerStart)
{
    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    ASSERT_NO_THROW(m_server->start());
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test server can stop
TEST_F(ServerTest, ServerStop)
{
    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NO_THROW(m_server->stop());
}

// Test basic TCP IPv4 connection using raw POSIX sockets
TEST_F(ServerTest, TCPIPv4Connection)
{
    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(test_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    EXPECT_EQ(ret, 0);

    close(sock);
}

// Test basic TCP IPv6 connection
TEST_F(ServerTest, TCPIPv6Connection)
{
    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET6, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in6 addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(test_port);
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    EXPECT_EQ(ret, 0);

    close(sock);
}

// Test TCP connection then close
TEST_F(ServerTest, TCPMessageProcessing)
{
    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(test_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(ret, 0);

    close(sock);
}

// Test UDP send/receive
TEST_F(ServerTest, UDPSendReceive)
{
    config::server_config config = make_test_server_config();
    uint16_t test_port = get_unique_port();
    config.network_port = test_port;
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config,
                                        std::make_unique<session_manager>(),
                                        std::make_unique<timer_manager>(*m_loop));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Send a test message (auth request)
    message_t auth_request;
    create_auth_request_message(&auth_request, "HUB", "test_user", "test_user", "test_hash");

    char json_buffer[BUFFER_SIZE];
    int result = serialize_message_to_json(&auth_request, json_buffer);
    ASSERT_EQ(result, 0);

    ssize_t bytes_sent = sendto(sock, json_buffer, strlen(json_buffer), 0,
                                reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    EXPECT_GT(bytes_sent, 0);

    close(sock);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
