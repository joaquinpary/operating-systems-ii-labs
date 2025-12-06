#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct session_info
{
    std::string session_id;
    bool is_authenticated;
    std::string client_type; // "HUB" or "WAREHOUSE"
    std::string username;
};

class session_manager
{
  public:
    session_manager();
    ~session_manager();

    // Generate a unique session ID for a new connection
    std::string create_session();

    // Mark a session as authenticated
    void mark_authenticated(const std::string& session_id, const std::string& client_type,
                            const std::string& username);

    // Check if a session is authenticated
    bool is_authenticated(const std::string& session_id) const;

    // Get session info
    std::unique_ptr<session_info> get_session_info(const std::string& session_id) const;

    // Remove a session (when connection closes)
    void remove_session(const std::string& session_id);

    // Get client type for a session
    std::string get_client_type(const std::string& session_id) const;

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, session_info> m_sessions;
    std::uint64_t m_session_counter;
};

#endif // SESSION_MANAGER_HPP

