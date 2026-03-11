#include "auth_module.hpp"
#include "connection_pool.hpp"

#include <iostream>

// AUTH MODULE CONSTRUCTOR
auth_module::auth_module(connection_pool& pool) : m_pool(pool)
{
}

auth_module::~auth_module()
{
}

auth_result auth_module::authenticate(const std::string& username, const std::string& password)
{
    auth_result result;
    result.status_code = auth_result_code::ERROR;
    result.client_type = "";
    result.username = "";
    result.error_message = "";

    try
    {
        auto guard = m_pool.acquire();
        auto cred = query_credentials_by_username(guard.get(), username);

        if (!cred)
        {
            // User not found
            result.status_code = auth_result_code::INVALID_CREDENTIALS;
            result.error_message = "Invalid username or password";
            return result;
        }

        // Compare password hash (assuming password is already hashed when sent)
        // TODO: If passwords are sent in plain text, implement hashing here
        if (cred->password_hash != password)
        {
            result.status_code = auth_result_code::INVALID_CREDENTIALS;
            result.error_message = "Invalid username or password";
            return result;
        }

        // Authentication successful
        result.status_code = auth_result_code::SUCCESS;
        result.client_type = cred->client_type;
        result.username = cred->username;

        // Mark client as active in the database
        set_client_active(guard.get(), username, true);

        return result;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Authentication error: " << ex.what() << std::endl;
        result.status_code = auth_result_code::ERROR;
        result.error_message = "Internal authentication error";
        return result;
    }
}

void auth_module::deactivate_client(const std::string& username)
{
    try
    {
        auto guard = m_pool.acquire();
        set_client_active(guard.get(), username, false);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error deactivating client " << username << ": " << ex.what() << std::endl;
    }
}
