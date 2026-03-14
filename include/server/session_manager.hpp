#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include "posix_address.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// Forward declaration
class tcp_session;

/// Prefix used when building TCP session identifiers.
inline constexpr const char* TCP_SESSION_PREFIX = "tcp_session_";
/// Prefix used when building UDP session identifiers.
inline constexpr const char* UDP_SESSION_PREFIX = "udp_";
/// Separator between UDP address components in session IDs.
inline constexpr char UDP_SESSION_SEPARATOR = '_';

struct session_info
{
    enum class connection_type
    {
        TCP,
        UDP
    };

    std::string session_id;
    bool is_authenticated;
    bool is_blacklisted;
    std::string client_type; // "HUB" or "WAREHOUSE"
    std::string username;
    connection_type type;

    // UDP-specific: endpoint for sending responses and retries
    std::optional<posix_address> udp_endpoint;

    // TCP-specific: weak reference to tcp_session for message resending
    std::weak_ptr<tcp_session> tcp_session_ref;

    session_info() : is_authenticated(false), is_blacklisted(false), type(connection_type::TCP)
    {
    }
};

/**
 * Manages sessions. Lives in the reactor process only — no mutex needed.
 */
class session_manager
{
  public:
    session_manager();
    ~session_manager();

    std::string create_session();
    std::string get_or_create_udp_session(const posix_address& endpoint);

    void mark_authenticated(const std::string& session_id, const std::string& client_type, const std::string& username);
    bool is_authenticated(const std::string& session_id) const;

    bool is_blacklisted(const std::string& session_id) const;
    void blacklist_session(const std::string& session_id);

    std::unique_ptr<session_info> get_session_info(const std::string& session_id) const;
    void remove_session(const std::string& session_id);
    std::string get_client_type(const std::string& session_id) const;
    std::string find_session_by_username(const std::string& username) const;
    bool has_session(const std::string& session_id) const;

    std::optional<posix_address> get_udp_endpoint(const std::string& session_id) const;
    void set_tcp_session(const std::string& session_id, std::weak_ptr<tcp_session> session);

  private:
    std::string make_udp_session_id(const posix_address& endpoint) const;
    std::unordered_map<std::string, session_info> m_sessions;
    std::unordered_map<std::string, std::string> m_username_to_session; ///< Reverse index: username → session_id
    std::uint64_t m_session_counter;
};

#endif // SESSION_MANAGER_HPP
