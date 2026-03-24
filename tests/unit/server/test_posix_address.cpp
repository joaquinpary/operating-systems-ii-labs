#include "posix_address.hpp"

#include <gtest/gtest.h>
#include <unordered_set>

class PosixAddressTest : public ::testing::Test
{
  protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(PosixAddressTest, DefaultConstructorZeroLength)
{
    posix_address addr;
    EXPECT_EQ(addr.sockaddr_len(), 0u);
    EXPECT_EQ(addr.port(), 0);
}

TEST_F(PosixAddressTest, ConstructFromIPv4String)
{
    posix_address addr("192.168.1.1", 8080);
    EXPECT_EQ(addr.family(), AF_INET);
    EXPECT_EQ(addr.port(), 8080);
    EXPECT_EQ(addr.ip_string(), "192.168.1.1");
    EXPECT_FALSE(addr.is_v6());
    EXPECT_EQ(addr.sockaddr_len(), sizeof(struct sockaddr_in));
}

TEST_F(PosixAddressTest, ConstructFromIPv6String)
{
    posix_address addr("::1", 9999);
    EXPECT_EQ(addr.family(), AF_INET6);
    EXPECT_EQ(addr.port(), 9999);
    EXPECT_EQ(addr.ip_string(), "::1");
    EXPECT_TRUE(addr.is_v6());
    EXPECT_EQ(addr.sockaddr_len(), sizeof(struct sockaddr_in6));
}

TEST_F(PosixAddressTest, ConstructFromInvalidIPThrows)
{
    EXPECT_THROW(posix_address("not_an_ip", 1234), std::runtime_error);
}

TEST_F(PosixAddressTest, FromIPv4StaticFactory)
{
    auto addr = posix_address::from_ipv4("10.0.0.1", 443);
    EXPECT_EQ(addr.family(), AF_INET);
    EXPECT_EQ(addr.port(), 443);
    EXPECT_EQ(addr.ip_string(), "10.0.0.1");
    EXPECT_FALSE(addr.is_v6());
}

TEST_F(PosixAddressTest, FromIPv4InvalidThrows)
{
    EXPECT_THROW(posix_address::from_ipv4("::1", 443), std::runtime_error);
}

TEST_F(PosixAddressTest, FromIPv6StaticFactory)
{
    auto addr = posix_address::from_ipv6("fe80::1", 80);
    EXPECT_EQ(addr.family(), AF_INET6);
    EXPECT_EQ(addr.port(), 80);
    EXPECT_TRUE(addr.is_v6());
}

TEST_F(PosixAddressTest, FromIPv6InvalidThrows)
{
    EXPECT_THROW(posix_address::from_ipv6("192.168.1.1", 80), std::runtime_error);
}

TEST_F(PosixAddressTest, ToStringIPv4)
{
    posix_address addr("127.0.0.1", 5000);
    EXPECT_EQ(addr.to_string(), "127.0.0.1:5000");
}

TEST_F(PosixAddressTest, ToStringIPv6)
{
    posix_address addr("::1", 5000);
    EXPECT_EQ(addr.to_string(), "::1:5000");
}

TEST_F(PosixAddressTest, EqualityIPv4)
{
    posix_address a("192.168.0.1", 8080);
    posix_address b("192.168.0.1", 8080);
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST_F(PosixAddressTest, InequalityDifferentPort)
{
    posix_address a("192.168.0.1", 8080);
    posix_address b("192.168.0.1", 9090);
    EXPECT_NE(a, b);
}

TEST_F(PosixAddressTest, InequalityDifferentIP)
{
    posix_address a("192.168.0.1", 8080);
    posix_address b("192.168.0.2", 8080);
    EXPECT_NE(a, b);
}

TEST_F(PosixAddressTest, InequalityDifferentFamily)
{
    posix_address a("127.0.0.1", 8080);
    posix_address b("::1", 8080);
    EXPECT_NE(a, b);
}

TEST_F(PosixAddressTest, EqualityIPv6)
{
    posix_address a("::1", 3000);
    posix_address b("::1", 3000);
    EXPECT_EQ(a, b);
}

TEST_F(PosixAddressTest, SockaddrPtrNotNull)
{
    posix_address addr("10.0.0.1", 1234);
    EXPECT_NE(addr.sockaddr_ptr(), nullptr);
    EXPECT_NE(addr.sockaddr_len_ptr(), nullptr);
}

TEST_F(PosixAddressTest, MutableSockaddrPtr)
{
    posix_address addr("10.0.0.1", 1234);
    struct sockaddr* ptr = addr.sockaddr_ptr_mut();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->sa_family, AF_INET);
}

TEST_F(PosixAddressTest, ConstructFromSockaddrStorage)
{
    struct sockaddr_storage storage{};
    auto* a4 = reinterpret_cast<struct sockaddr_in*>(&storage);
    a4->sin_family = AF_INET;
    a4->sin_port = htons(7777);
    inet_pton(AF_INET, "172.16.0.1", &a4->sin_addr);

    posix_address addr(storage, sizeof(struct sockaddr_in));
    EXPECT_EQ(addr.family(), AF_INET);
    EXPECT_EQ(addr.port(), 7777);
    EXPECT_EQ(addr.ip_string(), "172.16.0.1");
}

TEST_F(PosixAddressTest, HashableInUnorderedSet)
{
    std::unordered_set<posix_address> addrs;
    addrs.insert(posix_address("192.168.1.1", 80));
    addrs.insert(posix_address("192.168.1.1", 80));
    addrs.insert(posix_address("10.0.0.1", 80));
    EXPECT_EQ(addrs.size(), 2u);
}

TEST_F(PosixAddressTest, IPv4LoopbackVariants)
{
    posix_address a("127.0.0.1", 0);
    EXPECT_EQ(a.family(), AF_INET);
    EXPECT_EQ(a.port(), 0);
}

TEST_F(PosixAddressTest, IPv6FullForm)
{
    posix_address addr("0000:0000:0000:0000:0000:0000:0000:0001", 1234);
    EXPECT_TRUE(addr.is_v6());
    EXPECT_EQ(addr.ip_string(), "::1");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
