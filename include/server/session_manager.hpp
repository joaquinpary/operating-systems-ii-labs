#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include "posix_address.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class tcp_session;

/// Prefix used when building TCP session identifiers.
inline constexpr const char* TCP_SESSION_PREFIX = "tcp_session_";
/// Prefix used when building UDP session identifiers.
inline constexpr const char* UDP_SESSION_PREFIX = "udp_";
/// Separator between UDP address components in session IDs.
inline constexpr char UDP_SESSION_SEPARATOR = '_';

/** Metadata tracked for each live TCP or UDP session. */
struct session_info
{
    enum class connection_type
    {
        TCP,
        UDP
    };

    std::string session_id;                     ///< Stable internal session identifier.
    bool is_authenticated;                      ///< Whether AUTH_REQUEST completed successfully.
    bool is_blacklisted;                        ///< Whether the session has been blocked.
    std::string client_type;                    ///< HUB or WAREHOUSE once authenticated.
    std::string username;                       ///< Authenticated username, if any.
    connection_type type;                       ///< Transport backing the session.
    std::optional<posix_address> udp_endpoint;  ///< UDP endpoint used for replies and retries.
    std::weak_ptr<tcp_session> tcp_session_ref; ///< TCP session used for retransmissions.

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

    /** Create a new logical TCP session identifier. */
    std::string create_session();
    /** Resolve or create the logical session associated with a UDP endpoint. */
    std::string get_or_create_udp_session(const posix_address& endpoint);

    /** Persist the authenticated identity associated with a session. */
    void mark_authenticated(const std::string& session_id, const std::string& client_type, const std::string& username);
    /** Check whether a session completed authentication. */
    bool is_authenticated(const std::string& session_id) const;

    /** Check whether a session is blacklisted. */
    bool is_blacklisted(const std::string& session_id) const;
    /** Mark a session as blacklisted. */
    void blacklist_session(const std::string& session_id);

    /** Return a copy of the stored session metadata, if present. */
    std::unique_ptr<session_info> get_session_info(const std::string& session_id) const;
    /** Remove a session and all reverse indexes associated with it. */
    void remove_session(const std::string& session_id);
    /** Return the authenticated client type for a session, or an empty string. */
    std::string get_client_type(const std::string& session_id) const;
    /** Find the session currently associated with a username, or an empty string. */
    std::string find_session_by_username(const std::string& username) const;
    /** Check whether a session id exists. */
    bool has_session(const std::string& session_id) const;

    /** Return the UDP endpoint associated with a session when it exists. */
    std::optional<posix_address> get_udp_endpoint(const std::string& session_id) const;
    /** Associate a TCP session object with the session metadata for retries. */
    void set_tcp_session(const std::string& session_id, std::weak_ptr<tcp_session> session);

  private:
    std::string make_udp_session_id(const posix_address& endpoint) const;
    std::unordered_map<std::string, session_info> m_sessions;
    std::unordered_map<std::string, std::string> m_username_to_session; ///< Reverse index: username → session_id
    std::uint64_t m_session_counter;
};

#endif // SESSION_MANAGER_HPP
