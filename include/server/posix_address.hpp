#ifndef POSIX_ADDRESS_HPP
#define POSIX_ADDRESS_HPP

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>

/**
 * Lightweight wrapper around sockaddr_storage.
 * Replaces asio::ip::tcp::endpoint and asio::ip::udp::endpoint.
 * Pure value type — copyable, hashable.
 */
class posix_address
{
  public:
    posix_address();
    posix_address(const struct sockaddr_storage& addr, socklen_t len);
    posix_address(const std::string& ip, std::uint16_t port);

    /**
     * Create from an IPv4 address string + port.
     */
    static posix_address from_ipv4(const std::string& ip, std::uint16_t port);

    /**
     * Create from an IPv6 address string + port.
     */
    static posix_address from_ipv6(const std::string& ip, std::uint16_t port);

    /** Get address family (AF_INET or AF_INET6). */
    sa_family_t family() const;

    /** Get port number in host byte order. */
    std::uint16_t port() const;

    /** Human-readable IP string. */
    std::string ip_string() const;

    /** "ip:port" string. */
    std::string to_string() const;

    /** Is this an IPv6 address? */
    bool is_v6() const;

    /** Get pointer to the underlying sockaddr (for syscalls). */
    const struct sockaddr* sockaddr_ptr() const;
    struct sockaddr* sockaddr_ptr_mut();

    /** Get socklen_t for the underlying address. */
    socklen_t sockaddr_len() const;
    socklen_t* sockaddr_len_ptr();

    bool operator==(const posix_address& other) const;
    bool operator!=(const posix_address& other) const;

  private:
    struct sockaddr_storage m_addr;
    socklen_t m_len;
};

// std::hash specialization for use in unordered containers
namespace std
{
template <> struct hash<posix_address>
{
    std::size_t operator()(const posix_address& addr) const noexcept
    {
        std::size_t h = std::hash<int>{}(addr.family());
        h ^= std::hash<std::uint16_t>{}(addr.port()) << 1;
        h ^= std::hash<std::string>{}(addr.ip_string()) << 2;
        return h;
    }
};
} // namespace std

#endif // POSIX_ADDRESS_HPP
