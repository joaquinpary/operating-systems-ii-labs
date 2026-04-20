#ifndef AUTH_MODULE_HPP
#define AUTH_MODULE_HPP

#include "connection_pool.hpp"
#include "database.hpp"
#include <memory>
#include <string>

/** Result codes returned by the authentication module. */
enum class auth_result_code
{
    SUCCESS = 200,
    INVALID_CREDENTIALS = 401,
    ERROR = 500
};

/** Authentication outcome for a single login attempt. */
struct auth_result
{
    auth_result_code status_code; ///< Final authentication status.
    std::string client_type;      ///< Authenticated client role when login succeeds.
    std::string username;         ///< Canonical username returned by the credentials store.
    std::string error_message;    ///< Human-readable error detail for failed attempts.
};

/**
 * Validates client credentials against the database-backed credential store.
 */
class auth_module
{
  public:
    /** Build an authentication module that uses the provided connection pool. */
    explicit auth_module(connection_pool& pool);
    ~auth_module();

    /**
     * Authenticate a client with the stored password hash.
     * @param username Username to look up in the credentials table.
     * @param password Password hash provided by the client.
     * @return Authentication result with status, resolved role and optional error details.
     */
    auth_result authenticate(const std::string& username, const std::string& password);

    /**
     * Mark a client as inactive after disconnect or explicit session teardown.
     * @param username Username to deactivate in the credentials table.
     */
    void deactivate_client(const std::string& username);

  private:
    connection_pool& m_pool;
};

#endif // AUTH_MODULE_HPP
