#include "auth_module.hpp"

#include <iostream>

// AUTH MODULE CONSTRUCTOR
auth_module::auth_module(pqxx::connection& db_connection) : m_db_connection(db_connection)
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
        pqxx::work txn(m_db_connection);
        auto cred = query_credentials_by_username(txn, username);

        if (!cred)
        {
            // User not found
            result.status_code = auth_result_code::INVALID_CREDENTIALS;
            result.error_message = "Invalid username or password";
            return result;
        }

        // Check if user is active
        if (!cred->is_active)
        {
            result.status_code = auth_result_code::USER_INACTIVE;
            result.error_message = "User account is inactive";
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

