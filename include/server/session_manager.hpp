#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include <asio.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

// Forward declaration
class tcp_session;

struct session_info
{
    enum class connection_type { TCP, UDP };
    
    std::string session_id;
    bool is_authenticated;
    bool is_blacklisted;
    std::string client_type; // "HUB" or "WAREHOUSE"
    std::string username;
    connection_type type;
    
    // UDP-specific: endpoint for sending responses and retries
    std::optional<asio::ip::udp::endpoint> udp_endpoint;
    
    // TCP-specific: weak reference to tcp_session for message resending
    std::weak_ptr<tcp_session> tcp_session_ref;
    
    session_info() : is_authenticated(false), is_blacklisted(false), type(connection_type::TCP) {}
};

class session_manager
{
  public:
    session_manager();
    ~session_manager();

    // Generate a unique session ID for a new TCP connection
    std::string create_session();
    
    // Create or get UDP session from endpoint (deterministic session_id)
    std::string get_or_create_udp_session(const asio::ip::udp::endpoint& endpoint);

    // Mark a session as authenticated
    void mark_authenticated(const std::string& session_id, const std::string& client_type, const std::string& username);

    // Check if a session is authenticated
    bool is_authenticated(const std::string& session_id) const;
    
    // Check if a session is blacklisted (should ignore all messages)
    bool is_blacklisted(const std::string& session_id) const;
    
    // Blacklist a session (mark it to ignore all future messages)
    void blacklist_session(const std::string& session_id);

    // Get session info
    std::unique_ptr<session_info> get_session_info(const std::string& session_id) const;

    // Remove a session (when connection closes)
    void remove_session(const std::string& session_id);

    // Get client type for a session
    std::string get_client_type(const std::string& session_id) const;
    
    // UDP-specific methods
    
    // Get UDP endpoint for a session (for sending responses/retries)
    std::optional<asio::ip::udp::endpoint> get_udp_endpoint(const std::string& session_id) const;
    
    // Set TCP session reference for message resending (stores weak_ptr)
    void set_tcp_session(const std::string& session_id, std::weak_ptr<tcp_session> session);

  private:
    // Helper: Generate deterministic session_id from UDP endpoint
    std::string make_udp_session_id(const asio::ip::udp::endpoint& endpoint) const;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, session_info> m_sessions;
    std::uint64_t m_session_counter;
};

#endif // SESSION_MANAGER_HPP
