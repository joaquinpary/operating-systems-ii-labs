#include "server.hpp"
#include <asio.hpp>
#include <gtest/gtest.h>

using namespace asio::ip;

class NetworkTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_io_context.reset(new asio::io_context);
    }

    std::unique_ptr<asio::io_context> m_io_context;
    const std::string test_port = "9999";
};

// Test básico de eco TCP IPv4
TEST_F(NetworkTest, TCPIPv4Echo)
{
    server s(*m_io_context);

    std::thread server_thread([&]() { m_io_context->run(); });

    tcp::socket client(*m_io_context);
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 9999));

    const std::string test_data = "Hello TCPv4!";
    client.write_some(asio::buffer(test_data));

    std::array<char, 256> reply;
    size_t len = client.read_some(asio::buffer(reply));

    EXPECT_EQ(test_data, std::string(reply.data(), len));

    m_io_context->stop();
    server_thread.join();
}

// Test básico de eco TCP IPv6
TEST_F(NetworkTest, TCPIPv6Echo)
{
    server s(*m_io_context);

    std::thread server_thread([&]() { m_io_context->run(); });

    tcp::socket client(*m_io_context);
    client.connect(tcp::endpoint(asio::ip::make_address("::1"), 9999));

    const std::string test_data = "Hello TCPv6!";
    client.write_some(asio::buffer(test_data));

    std::array<char, 256> reply;
    size_t len = client.read_some(asio::buffer(reply));

    EXPECT_EQ(test_data, std::string(reply.data(), len));

    m_io_context->stop();
    server_thread.join();
}

// Test básico de eco UDP IPv6
TEST_F(NetworkTest, UDPIPv6Echo)
{
    server s(*m_io_context);

    std::thread server_thread([&]() { m_io_context->run(); });

    udp::socket client(*m_io_context, udp::endpoint(asio::ip::address_v6::any(), 0));
    udp::endpoint server_endpoint(asio::ip::make_address("::1"), 9999);

    const std::string test_data = "Hello UDPv6!";
    client.send_to(asio::buffer(test_data), server_endpoint);

    std::array<char, 256> reply;
    udp::endpoint sender_endpoint;
    size_t len = client.receive_from(asio::buffer(reply), sender_endpoint);

    EXPECT_EQ(test_data, std::string(reply.data(), len));

    m_io_context->stop();
    server_thread.join();
}

// Test básico de eco UDP IPv4
TEST_F(NetworkTest, UDPIPv4Echo)
{
    server s(*m_io_context);

    std::thread server_thread([&]() { m_io_context->run(); });

    udp::socket client(*m_io_context, udp::endpoint(asio::ip::address_v4::any(), 0));
    udp::endpoint server_endpoint(asio::ip::make_address("127.0.0.1"), 9999);

    const std::string test_data = "Hello UDPv4!";
    client.send_to(asio::buffer(test_data), server_endpoint);

    std::array<char, 256> reply;
    udp::endpoint sender_endpoint;
    size_t len = client.receive_from(asio::buffer(reply), sender_endpoint);

    EXPECT_EQ(test_data, std::string(reply.data(), len));

    m_io_context->stop();
    server_thread.join();
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
