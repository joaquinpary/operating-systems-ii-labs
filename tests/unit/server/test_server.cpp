#include <gtest/gtest.h>
#include <asio.hpp>
#include "server.hpp"
#include <thread>
#include <chrono>

using namespace asio::ip;

class ServerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_io_context = std::make_unique<asio::io_context>();
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
    }

    void start_io_context()
    {
        m_io_thread = std::make_unique<std::thread>([this]() { m_io_context->run(); });
    }

    std::unique_ptr<asio::io_context> m_io_context;
    std::unique_ptr<server> m_server;
    std::unique_ptr<std::thread> m_io_thread;
};

// Test default configuration values
TEST_F(ServerTest, DefaultConfigValues)
{
    server_config config = make_default_server_config();

    EXPECT_EQ(config.tcp.address_v4, server_constants::DEFAULT_IPV4_ADDRESS);
    EXPECT_EQ(config.tcp.address_v6, server_constants::DEFAULT_IPV6_ADDRESS);
    EXPECT_EQ(config.tcp.port, server_constants::DEFAULT_PORT);
    EXPECT_EQ(config.udp.address_v4, server_constants::DEFAULT_IPV4_ADDRESS);
    EXPECT_EQ(config.udp.address_v6, server_constants::DEFAULT_IPV6_ADDRESS);
    EXPECT_EQ(config.udp.port, server_constants::DEFAULT_PORT);
}

// Test server initialization with default configuration
TEST_F(ServerTest, ServerInitialization)
{
    ASSERT_NO_THROW({ m_server = std::make_unique<server>(*m_io_context); });
    ASSERT_NE(m_server, nullptr);
}

// Test server can start
TEST_F(ServerTest, ServerStart)
{
    m_server = std::make_unique<server>(*m_io_context);
    ASSERT_NO_THROW(m_server->start());
    start_io_context();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test server can stop
TEST_F(ServerTest, ServerStop)
{
    m_server = std::make_unique<server>(*m_io_context);
    m_server->start();
    start_io_context();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_NO_THROW(m_server->stop());
}

// Test basic TCP IPv4 connection
TEST_F(ServerTest, TCPIPv4Connection)
{
    m_server = std::make_unique<server>(*m_io_context);
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), server_constants::DEFAULT_PORT), ec);

    EXPECT_FALSE(ec);
    if (!ec)
    {
        client.close();
    }
}

// Test basic TCP IPv6 connection
TEST_F(ServerTest, TCPIPv6Connection)
{
    m_server = std::make_unique<server>(*m_io_context);
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("::1"), server_constants::DEFAULT_PORT), ec);

    EXPECT_FALSE(ec);
    if (!ec)
    {
        client.close();
    }
}

// Test server responds with echo in TCP
TEST_F(ServerTest, TCPEchoResponse)
{
    m_server = std::make_unique<server>(*m_io_context);
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    tcp::socket client(*m_io_context);
    asio::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), server_constants::DEFAULT_PORT), ec);
    ASSERT_FALSE(ec);

    const std::string test_message = "Hello TCP!";
    std::size_t bytes_written = asio::write(client, asio::buffer(test_message), ec);
    EXPECT_GT(bytes_written, 0);
    EXPECT_FALSE(ec);

    std::array<char, 256> response;
    std::size_t bytes_read = client.read_some(asio::buffer(response), ec);
    EXPECT_GT(bytes_read, 0);
    EXPECT_FALSE(ec);
    EXPECT_EQ(test_message, std::string(response.data(), bytes_read));

    client.close();
}

// Test server responds with echo in UDP
TEST_F(ServerTest, UDPEchoResponse)
{
    m_server = std::make_unique<server>(*m_io_context);
    m_server->start();
    start_io_context();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    udp::socket client(*m_io_context, udp::endpoint(udp::v4(), 0));
    udp::endpoint server_endpoint(asio::ip::make_address("127.0.0.1"), server_constants::DEFAULT_PORT);

    const std::string test_message = "Hello UDP!";
    std::size_t bytes_sent = client.send_to(asio::buffer(test_message), server_endpoint);
    EXPECT_GT(bytes_sent, 0);

    std::array<char, 256> response;
    udp::endpoint sender_endpoint;
    asio::error_code ec;
    std::size_t bytes_received = client.receive_from(asio::buffer(response), sender_endpoint, 0, ec);
    EXPECT_GT(bytes_received, 0);
    EXPECT_FALSE(ec);
    EXPECT_EQ(test_message, std::string(response.data(), bytes_received));
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

