#ifndef AUTH_MODULE_HPP
#define AUTH_MODULE_HPP

#include "connection_pool.hpp"
#include "database.hpp"
#include <memory>
#include <string>

// Authentication result codes
enum class auth_result_code
{
    SUCCESS = 200,             // Authentication successful
    INVALID_CREDENTIALS = 401, // Invalid username or password
    USER_INACTIVE = 403,       // User exists but is inactive
    ERROR = 500                // Internal error
};

struct auth_result
{
    auth_result_code status_code;
    std::string client_type; // "HUB" or "WAREHOUSE" if successful
    std::string username;
    std::string error_message;
};

class auth_module
{
  public:
    explicit auth_module(connection_pool& pool);
    ~auth_module();

    // Authenticate a user with username and password
    // password should be the hash to compare against password_hash in DB
    auth_result authenticate(const std::string& username, const std::string& password);

  private:
    connection_pool& m_pool;
};

#endif // AUTH_MODULE_HPP
