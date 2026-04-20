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
        // Stop the event loop FIRST and join its thread so no callbacks fire
        // during server teardown (avoids races on fd map and closed sockets).
        if (m_loop)
        {
            m_loop->stop();
        }
        if (m_io_thread && m_io_thread->joinable())
        {
            m_io_thread->join();
        }
        // Now safe to tear down the server (no concurrent epoll activity)
        if (m_server)
        {
            m_server->stop();
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

    // Create a server keeping raw pointers to session_manager and timer_manager
    void create_server_with_refs(uint16_t port)
    {
        auto config = make_test_server_config();
        config.network_port = port;
        auto sm = std::make_unique<session_manager>();
        auto tm = std::make_unique<timer_manager>(*m_loop);
        m_sm_ptr = sm.get();
        m_tm_ptr = tm.get();
        m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::move(sm), std::move(tm));
    }

    // Connect a TCP client to 127.0.0.1:port and return the socket fd
    int connect_tcp(uint16_t port)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr
        {
        };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            ::close(sock);
            return -1;
        }
        return sock;
    }

    // Send a BUFFER_SIZE frame over TCP (the server's wire format)
    void send_frame(int sock, const char* json)
    {
        char buf[BUFFER_SIZE]{};
        std::memcpy(buf, json, std::min(std::strlen(json), static_cast<std::size_t>(BUFFER_SIZE - 1)));
        ::send(sock, buf, BUFFER_SIZE, 0);
    }

    // Read a BUFFER_SIZE frame from TCP
    bool recv_frame(int sock, char* out, int timeout_ms = 500)
    {
        struct timeval tv
        {
        };
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::size_t total = 0;
        while (total < BUFFER_SIZE)
        {
            ssize_t n = recv(sock, out + total, BUFFER_SIZE - total, 0);
            if (n <= 0)
                return false;
            total += static_cast<std::size_t>(n);
        }
        return true;
    }

    // Push a response into the shared queue and signal the eventfd
    void push_response(const response_slot_t& resp)
    {
        m_shm->push_response(resp, m_response_efd);
    }

    // Try to consume a request from the shared queue (with timeout).
    // Runs wait_request in a detached helper so we don't block forever.
    bool try_pop_request(request_slot_t& out, int timeout_ms = 1000)
    {
        std::atomic<bool> done{false};
        request_slot_t tmp{};
        bool ok = false;
        std::thread t([&]() {
            ok = m_shm->wait_request(tmp);
            done.store(true);
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (done.load())
        {
            t.join();
            if (ok)
                out = tmp;
            return ok;
        }
        // Still blocked — signal shutdown so the thread can exit
        m_shm->signal_shutdown();
        t.join();
        return false;
    }

    std::unique_ptr<event_loop> m_loop;
    std::unique_ptr<shared_queue> m_shm;
    int m_response_efd = -1;
    std::unique_ptr<server> m_server;
    std::unique_ptr<std::thread> m_io_thread;
    std::unique_ptr<pqxx::connection> m_db_connection;
    std::shared_ptr<connection_pool> m_db_pool;
    session_manager* m_sm_ptr = nullptr;
    timer_manager* m_tm_ptr = nullptr;
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
        m_server =
            std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
                                     std::make_unique<timer_manager>(*m_loop));
    });
    ASSERT_NE(m_server, nullptr);
}

// Test server can start
TEST_F(ServerTest, ServerStart)
{
    config::server_config config = make_test_server_config();
    config.network_port = get_unique_port();
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
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
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
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
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
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
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
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
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::make_unique<session_manager>(),
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

// ====================== TCP Message Processing ======================

TEST_F(ServerTest, TCPSendMessageAppearsInQueue)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "test_user", "test_user", "hash");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&auth_msg, json);

    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    EXPECT_STRNE(req.session_id, "");
    EXPECT_EQ(req.protocol, static_cast<std::uint8_t>(protocol_type::TCP));

    ::close(sock);
}

TEST_F(ServerTest, TCPPeerCloseGeneratesDisconnect)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Authenticate the session so notify_disconnect fires
    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "tuser", "tuser", "hash");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&auth_msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Get the session_id from the queue
    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Mark the session authenticated (via response dispatch)
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "tuser", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Close the client socket — triggers on_readable n==0 path
    ::close(sock);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // A disconnect request should appear in the queue
    request_slot_t disc{};
    bool found_disconnect = false;
    while (try_pop_request(disc))
    {
        if (disc.is_disconnect)
        {
            found_disconnect = true;
            break;
        }
    }
    EXPECT_TRUE(found_disconnect);
    if (found_disconnect)
    {
        EXPECT_STREQ(disc.username, "tuser");
    }
}

// ====================== Response Dispatch ======================

TEST_F(ServerTest, DispatchSendToTCPSession)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send a message so we can get the session_id
    message_t msg;
    create_auth_request_message(&msg, "HUB", "u1", "u1", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate so send_to_session can resolve the TCP session
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "u1", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Push a SEND response
    const char* payload = "{\"type\":\"ack\"}";
    response_slot_t send_resp{};
    send_resp.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(send_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::memcpy(send_resp.payload, payload, std::strlen(payload));
    send_resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    push_response(send_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read the frame from the client
    char recv_buf[BUFFER_SIZE]{};
    ASSERT_TRUE(recv_frame(sock, recv_buf));
    EXPECT_STREQ(recv_buf, payload);

    ::close(sock);
}

TEST_F(ServerTest, DispatchSendByTargetUsername)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "myuser", "myuser", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "myuser", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // SEND with empty session_id, using target_username
    const char* payload = "{\"resolved\":true}";
    response_slot_t send_resp{};
    send_resp.command = static_cast<std::uint8_t>(response_command::SEND);
    // session_id left empty
    std::strncpy(send_resp.target_username, "myuser", CREDENTIALS_SIZE - 1);
    std::memcpy(send_resp.payload, payload, std::strlen(payload));
    send_resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    push_response(send_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char recv_buf[BUFFER_SIZE]{};
    ASSERT_TRUE(recv_frame(sock, recv_buf));
    EXPECT_STREQ(recv_buf, payload);

    ::close(sock);
}

TEST_F(ServerTest, DispatchSendWithAckTimer)
{
    uint16_t port = get_unique_port();
    auto config = make_test_server_config();
    config.network_port = port;
    config.ack_timeout = 5;
    auto sm = std::make_unique<session_manager>();
    auto tm = std::make_unique<timer_manager>(*m_loop);
    m_sm_ptr = sm.get();
    m_tm_ptr = tm.get();
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::move(sm), std::move(tm));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "su", "su", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "su", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // SEND with start_ack_timer=true
    const char* payload = "{\"ack\":1}";
    response_slot_t send_resp{};
    send_resp.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(send_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::memcpy(send_resp.payload, payload, std::strlen(payload));
    send_resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    send_resp.start_ack_timer = true;
    send_resp.timer_timeout = 5;
    std::strncpy(send_resp.timer_key, "ts_123", TIMESTAMP_SIZE - 1);
    send_resp.retry_count = 0;
    send_resp.max_retries = 3;
    push_response(send_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Client should receive the data
    char recv_buf[BUFFER_SIZE]{};
    ASSERT_TRUE(recv_frame(sock, recv_buf));
    EXPECT_STREQ(recv_buf, payload);

    // Cancel the timer so it doesn't fire during teardown
    response_slot_t cancel_resp{};
    cancel_resp.command = static_cast<std::uint8_t>(response_command::CANCEL_ACK_TIMER);
    std::strncpy(cancel_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(cancel_resp.timer_key, "ts_123", TIMESTAMP_SIZE - 1);
    push_response(cancel_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ::close(sock);
}

TEST_F(ServerTest, DispatchMarkAuthenticated)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "au", "au", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    EXPECT_FALSE(m_sm_ptr->is_authenticated(session_id));

    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(resp.client_type, "WAREHOUSE", ROLE_SIZE - 1);
    std::strncpy(resp.username, "au", CREDENTIALS_SIZE - 1);
    push_response(resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(m_sm_ptr->is_authenticated(session_id));
    EXPECT_EQ(m_sm_ptr->get_client_type(session_id), "WAREHOUSE");

    ::close(sock);
}

TEST_F(ServerTest, DispatchBlacklist)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "bu", "bu", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    EXPECT_FALSE(m_sm_ptr->is_blacklisted(session_id));

    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::BLACKLIST);
    std::strncpy(resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    push_response(resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(m_sm_ptr->is_blacklisted(session_id));

    ::close(sock);
}

TEST_F(ServerTest, DispatchStartAndCancelAckTimer)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "tu", "tu", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // START_ACK_TIMER
    response_slot_t start_resp{};
    start_resp.command = static_cast<std::uint8_t>(response_command::START_ACK_TIMER);
    std::strncpy(start_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(start_resp.timer_key, "ts_456", TIMESTAMP_SIZE - 1);
    start_resp.timer_timeout = 10;
    start_resp.retry_count = 0;
    start_resp.max_retries = 3;
    const char* payload = "{\"x\":1}";
    std::memcpy(start_resp.payload, payload, std::strlen(payload));
    start_resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    push_response(start_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // CANCEL_ACK_TIMER
    response_slot_t cancel_resp{};
    cancel_resp.command = static_cast<std::uint8_t>(response_command::CANCEL_ACK_TIMER);
    std::strncpy(cancel_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(cancel_resp.timer_key, "ts_456", TIMESTAMP_SIZE - 1);
    push_response(cancel_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Session should still exist (timer was cancelled, no disconnect)
    EXPECT_TRUE(m_sm_ptr->has_session(session_id));

    ::close(sock);
}

TEST_F(ServerTest, DispatchClearTimers)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "cu", "cu", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::CLEAR_TIMERS);
    std::strncpy(resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    push_response(resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(m_sm_ptr->has_session(session_id));

    ::close(sock);
}

TEST_F(ServerTest, DispatchStartAndResetKeepaliveTimer)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "ku", "ku", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate first
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "ku", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // START_KEEPALIVE_TIMER with large timeout so it won't fire during test
    response_slot_t ka_resp{};
    ka_resp.command = static_cast<std::uint8_t>(response_command::START_KEEPALIVE_TIMER);
    std::strncpy(ka_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    ka_resp.timer_timeout = 30;
    push_response(ka_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // RESET_KEEPALIVE_TIMER
    response_slot_t reset_resp{};
    reset_resp.command = static_cast<std::uint8_t>(response_command::RESET_KEEPALIVE_TIMER);
    std::strncpy(reset_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    push_response(reset_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(m_sm_ptr->has_session(session_id));

    // Clean up: clear timers so they don't fire during teardown
    response_slot_t clear_resp{};
    clear_resp.command = static_cast<std::uint8_t>(response_command::CLEAR_TIMERS);
    std::strncpy(clear_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    push_response(clear_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ::close(sock);
}

TEST_F(ServerTest, DispatchDisconnectRemovesSession)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "du", "du", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "du", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(m_sm_ptr->has_session(session_id));

    // DISCONNECT
    response_slot_t disc_resp{};
    disc_resp.command = static_cast<std::uint8_t>(response_command::DISCONNECT);
    std::strncpy(disc_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    push_response(disc_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_FALSE(m_sm_ptr->has_session(session_id));

    ::close(sock);
}

TEST_F(ServerTest, DispatchBroadcast)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect two clients
    int sock1 = connect_tcp(port);
    ASSERT_GE(sock1, 0);
    int sock2 = connect_tcp(port);
    ASSERT_GE(sock2, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send messages to get session IDs
    message_t msg1, msg2;
    create_auth_request_message(&msg1, "HUB", "b1", "b1", "h");
    create_auth_request_message(&msg2, "WAREHOUSE", "b2", "b2", "h");
    char json1[BUFFER_SIZE]{}, json2[BUFFER_SIZE]{};
    serialize_message_to_json(&msg1, json1);
    serialize_message_to_json(&msg2, json2);
    send_frame(sock1, json1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    send_frame(sock2, json2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req1{}, req2{};
    ASSERT_TRUE(try_pop_request(req1));
    ASSERT_TRUE(try_pop_request(req2));
    std::string sid1(req1.session_id), sid2(req2.session_id);

    // Authenticate both
    for (auto& [sid, ctype, uname] :
         std::vector<std::tuple<std::string, const char*, const char*>>{{sid1, "HUB", "b1"}, {sid2, "WAREHOUSE", "b2"}})
    {
        response_slot_t auth_resp{};
        auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
        std::strncpy(auth_resp.session_id, sid.c_str(), SESSION_ID_SIZE - 1);
        std::strncpy(auth_resp.client_type, ctype, ROLE_SIZE - 1);
        std::strncpy(auth_resp.username, uname, CREDENTIALS_SIZE - 1);
        push_response(auth_resp);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Broadcast
    const char* payload = "{\"broadcast\":true}";
    response_slot_t bcast{};
    bcast.command = static_cast<std::uint8_t>(response_command::BROADCAST);
    std::memcpy(bcast.payload, payload, std::strlen(payload));
    bcast.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    push_response(bcast);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Both clients should receive the broadcast
    char recv1[BUFFER_SIZE]{}, recv2[BUFFER_SIZE]{};
    EXPECT_TRUE(recv_frame(sock1, recv1));
    EXPECT_TRUE(recv_frame(sock2, recv2));
    EXPECT_STREQ(recv1, payload);
    EXPECT_STREQ(recv2, payload);

    ::close(sock1);
    ::close(sock2);
}

TEST_F(ServerTest, KeepaliveTimeoutDisconnectsSession)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "ktu", "ktu", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "ktu", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Start a keepalive timer with short timeout (1s)
    response_slot_t ka_resp{};
    ka_resp.command = static_cast<std::uint8_t>(response_command::START_KEEPALIVE_TIMER);
    std::strncpy(ka_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    ka_resp.timer_timeout = 1;
    push_response(ka_resp);

    // Wait for the timer to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Session should be removed
    EXPECT_FALSE(m_sm_ptr->has_session(session_id));

    ::close(sock);
}

TEST_F(ServerTest, AckTimeoutRetriesAndDisconnects)
{
    uint16_t port = get_unique_port();
    auto config = make_test_server_config();
    config.network_port = port;
    config.ack_timeout = 1;
    config.max_retries = 2;
    auto sm = std::make_unique<session_manager>();
    auto tm = std::make_unique<timer_manager>(*m_loop);
    m_sm_ptr = sm.get();
    m_tm_ptr = tm.get();
    m_server = std::make_unique<server>(*m_loop, *m_shm, m_response_efd, config, std::move(sm), std::move(tm));
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = connect_tcp(port);
    ASSERT_GE(sock, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    message_t msg;
    create_auth_request_message(&msg, "HUB", "atu", "atu", "h");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&msg, json);
    send_frame(sock, json);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    std::string session_id(req.session_id);

    // Authenticate
    response_slot_t auth_resp{};
    auth_resp.command = static_cast<std::uint8_t>(response_command::MARK_AUTHENTICATED);
    std::strncpy(auth_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::strncpy(auth_resp.client_type, "HUB", ROLE_SIZE - 1);
    std::strncpy(auth_resp.username, "atu", CREDENTIALS_SIZE - 1);
    push_response(auth_resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // SEND + ack timer with retry_count=0, max_retries=2, timeout=1s
    const char* payload = "{\"need_ack\":1}";
    response_slot_t send_resp{};
    send_resp.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(send_resp.session_id, session_id.c_str(), SESSION_ID_SIZE - 1);
    std::memcpy(send_resp.payload, payload, std::strlen(payload));
    send_resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    send_resp.start_ack_timer = true;
    send_resp.timer_timeout = 1;
    std::strncpy(send_resp.timer_key, "ack_ts", TIMESTAMP_SIZE - 1);
    send_resp.retry_count = 0;
    send_resp.max_retries = 2;
    push_response(send_resp);

    // Read the initial send
    char recv_buf[BUFFER_SIZE]{};
    EXPECT_TRUE(recv_frame(sock, recv_buf));

    // Wait for retry #1 (at 1s)
    char retry_buf[BUFFER_SIZE]{};
    EXPECT_TRUE(recv_frame(sock, retry_buf, 2000));
    EXPECT_STREQ(retry_buf, payload);

    // After max retries, the session should be disconnected
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_FALSE(m_sm_ptr->has_session(session_id));

    ::close(sock);
}

TEST_F(ServerTest, UDPMessageAppearsInQueue)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in server_addr
    {
    };
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    message_t auth_msg;
    create_auth_request_message(&auth_msg, "HUB", "udp_user", "udp_user", "hash");
    char json[BUFFER_SIZE]{};
    serialize_message_to_json(&auth_msg, json);

    ssize_t sent =
        sendto(sock, json, std::strlen(json), 0, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    EXPECT_GT(sent, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    request_slot_t req{};
    ASSERT_TRUE(try_pop_request(req));
    EXPECT_EQ(req.protocol, static_cast<std::uint8_t>(protocol_type::UDP));
    EXPECT_STRNE(req.session_id, "");

    ::close(sock);
}

TEST_F(ServerTest, DispatchResponseToGoneSessionIsIgnored)
{
    uint16_t port = get_unique_port();
    create_server_with_refs(port);
    m_server->start();
    start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Push a SEND for a non-existent session — should be silently ignored
    response_slot_t resp{};
    resp.command = static_cast<std::uint8_t>(response_command::SEND);
    std::strncpy(resp.session_id, "nonexistent_session", SESSION_ID_SIZE - 1);
    const char* payload = "{\"x\":1}";
    std::memcpy(resp.payload, payload, std::strlen(payload));
    resp.payload_len = static_cast<std::uint32_t>(std::strlen(payload));
    push_response(resp);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // No crash, no hang — test passes if we get here
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
