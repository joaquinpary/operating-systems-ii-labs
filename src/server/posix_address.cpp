#include "posix_address.hpp"

#include <stdexcept>

posix_address::posix_address() : m_len(0)
{
    std::memset(&m_addr, 0, sizeof(m_addr));
}

posix_address::posix_address(const struct sockaddr_storage& addr, socklen_t len) : m_addr(addr), m_len(len)
{
}

posix_address::posix_address(const std::string& ip, std::uint16_t port)
{
    std::memset(&m_addr, 0, sizeof(m_addr));

    auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&m_addr);
    if (inet_pton(AF_INET, ip.c_str(), &addr4->sin_addr) == 1)
    {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        m_len = sizeof(struct sockaddr_in);
        return;
    }

    auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&m_addr);
    std::memset(&m_addr, 0, sizeof(m_addr));
    if (inet_pton(AF_INET6, ip.c_str(), &addr6->sin6_addr) == 1)
    {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        m_len = sizeof(struct sockaddr_in6);
        return;
    }

    throw std::runtime_error("Invalid IP address: " + ip);
}

posix_address posix_address::from_ipv4(const std::string& ip, std::uint16_t port)
{
    posix_address addr;
    auto* a4 = reinterpret_cast<struct sockaddr_in*>(&addr.m_addr);
    a4->sin_family = AF_INET;
    a4->sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &a4->sin_addr) != 1)
    {
        throw std::runtime_error("Invalid IPv4 address: " + ip);
    }
    addr.m_len = sizeof(struct sockaddr_in);
    return addr;
}

posix_address posix_address::from_ipv6(const std::string& ip, std::uint16_t port)
{
    posix_address addr;
    auto* a6 = reinterpret_cast<struct sockaddr_in6*>(&addr.m_addr);
    a6->sin6_family = AF_INET6;
    a6->sin6_port = htons(port);
    if (inet_pton(AF_INET6, ip.c_str(), &a6->sin6_addr) != 1)
    {
        throw std::runtime_error("Invalid IPv6 address: " + ip);
    }
    addr.m_len = sizeof(struct sockaddr_in6);
    return addr;
}

sa_family_t posix_address::family() const
{
    return m_addr.ss_family;
}

std::uint16_t posix_address::port() const
{
    if (m_addr.ss_family == AF_INET)
    {
        return ntohs(reinterpret_cast<const struct sockaddr_in*>(&m_addr)->sin_port);
    }
    if (m_addr.ss_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&m_addr)->sin6_port);
    }
    return 0;
}

std::string posix_address::ip_string() const
{
    char buf[INET6_ADDRSTRLEN] = {};
    if (m_addr.ss_family == AF_INET)
    {
        inet_ntop(AF_INET, &reinterpret_cast<const struct sockaddr_in*>(&m_addr)->sin_addr, buf, sizeof(buf));
    }
    else if (m_addr.ss_family == AF_INET6)
    {
        inet_ntop(AF_INET6, &reinterpret_cast<const struct sockaddr_in6*>(&m_addr)->sin6_addr, buf, sizeof(buf));
    }
    return std::string(buf);
}

std::string posix_address::to_string() const
{
    return ip_string() + ":" + std::to_string(port());
}

bool posix_address::is_v6() const
{
    return m_addr.ss_family == AF_INET6;
}

const struct sockaddr* posix_address::sockaddr_ptr() const
{
    return reinterpret_cast<const struct sockaddr*>(&m_addr);
}

struct sockaddr* posix_address::sockaddr_ptr_mut()
{
    return reinterpret_cast<struct sockaddr*>(&m_addr);
}

socklen_t posix_address::sockaddr_len() const
{
    return m_len;
}

socklen_t* posix_address::sockaddr_len_ptr()
{
    return &m_len;
}

bool posix_address::operator==(const posix_address& other) const
{
    if (m_addr.ss_family != other.m_addr.ss_family)
    {
        return false;
    }
    if (m_addr.ss_family == AF_INET)
    {
        auto* a = reinterpret_cast<const struct sockaddr_in*>(&m_addr);
        auto* b = reinterpret_cast<const struct sockaddr_in*>(&other.m_addr);
        return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    if (m_addr.ss_family == AF_INET6)
    {
        auto* a = reinterpret_cast<const struct sockaddr_in6*>(&m_addr);
        auto* b = reinterpret_cast<const struct sockaddr_in6*>(&other.m_addr);
        return a->sin6_port == b->sin6_port && std::memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(struct in6_addr)) == 0;
    }
    return false;
}

bool posix_address::operator!=(const posix_address& other) const
{
    return !(*this == other);
}
