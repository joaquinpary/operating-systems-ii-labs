#include "auth_module.hpp"
#include "mem_store.hpp"

#include <iostream>

// AUTH MODULE CONSTRUCTOR
auth_module::auth_module(mem_store& store) : m_store(store)
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
        auto cred = m_store.get_credential(username);

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

        // Mark client as active (cache + DB write-through)
        m_store.set_active(username, true);

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
    m_store.set_active(username, false);
}
