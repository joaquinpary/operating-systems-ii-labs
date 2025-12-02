#include "database.hpp"

#include <cJSON.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr const char* DEFAULT_DB_HOST = "localhost";
constexpr const char* DEFAULT_DB_NAME = "dhl_db";
constexpr const char* DEFAULT_DB_USER = "dhl_user";
constexpr const char* DEFAULT_DB_PASSWORD = "dhl_pass";
constexpr int DEFAULT_DB_PORT = 5432;

std::string get_env_var(const char* env_var, const char* default_value)
{
    const char* value = std::getenv(env_var);
    return value ? std::string(value) : std::string(default_value);
}

std::string build_connection_string()
{
    // Environment variables take precedence (used in Docker)
    // Fallback to default values (used for local development)
    std::string host = get_env_var("POSTGRES_HOST", DEFAULT_DB_HOST);
    std::string db = get_env_var("POSTGRES_DB", DEFAULT_DB_NAME);
    std::string user = get_env_var("POSTGRES_USER", DEFAULT_DB_USER);
    std::string password = get_env_var("POSTGRES_PASSWORD", DEFAULT_DB_PASSWORD);

    // Port can also be overridden by environment variable
    int port = DEFAULT_DB_PORT;
    const char* port_env = std::getenv("POSTGRES_PORT");
    if (port_env)
    {
        try
        {
            port = std::stoi(port_env);
        }
        catch (const std::exception&)
        {
            // Invalid port in env var, use default value
        }
    }

    return "host=" + host + " dbname=" + db + " user=" + user + " password=" + password +
           " port=" + std::to_string(port);
}
} // namespace

std::unique_ptr<pqxx::connection> connect_to_database()
{
    try
    {
        std::string conn_string = build_connection_string();
        auto conn = std::make_unique<pqxx::connection>(conn_string);

        if (conn->is_open())
        {
            std::cout << "Successfully connected to PostgreSQL database: " << conn->dbname() << std::endl;
            return conn;
        }
        else
        {
            std::cerr << "Failed to open database connection." << std::endl;
            return nullptr;
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Database connection error: " << ex.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<pqxx::connection> initialize_database()
{
    auto conn = connect_to_database();
    if (!conn)
    {
        return nullptr;
    }

    try
    {
        pqxx::work txn(*conn);
        if (create_credentials_table(txn) != 0)
        {
            std::cerr << "Failed to initialize database tables." << std::endl;
            return nullptr;
        }

        // Populate credentials table from JSON file if it exists
        const std::string credentials_file = "config/credentials.json";
        if (populate_credentials_table(*conn, credentials_file) != 0)
        {
            // Non-fatal: credentials file might not exist or might be empty
            std::cout << "Note: Could not populate credentials table from " << credentials_file << std::endl;
        }

        std::cout << "Database initialized successfully." << std::endl;
        return conn;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Database initialization error: " << ex.what() << std::endl;
        return nullptr;
    }
}

int create_credentials_table(pqxx::work& txn)
{
    try
    {
        std::string sql = "CREATE TABLE IF NOT EXISTS credentials ("
                          "id SERIAL PRIMARY KEY, "
                          "username TEXT UNIQUE NOT NULL, "
                          "password_hash TEXT NOT NULL, "
                          "client_type TEXT NOT NULL, "
                          "is_active BOOLEAN DEFAULT true"
                          ");";

        txn.exec(sql);
        txn.commit();
        std::cout << "Credentials table created successfully." << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error creating credentials table: " << ex.what() << std::endl;
        return 1;
    }
}

std::unique_ptr<credential> query_credentials_by_username(pqxx::work& txn, const std::string& username)
{
    try
    {
        std::string sql =
            "SELECT id, username, password_hash, client_type, is_active FROM credentials WHERE username = $1";
        pqxx::result result = txn.exec(pqxx::zview(sql), pqxx::params{username});

        if (result.empty())
        {
            return nullptr;
        }

        auto cred = std::make_unique<credential>();
        cred->id = result[0][0].as<int>();
        cred->username = result[0][1].as<std::string>();
        cred->password_hash = result[0][2].as<std::string>();
        cred->client_type = result[0][3].as<std::string>();
        cred->is_active = result[0][4].as<bool>();

        return cred;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error querying credentials: " << ex.what() << std::endl;
        return nullptr;
    }
}

int populate_credentials_table(pqxx::connection& conn, const std::string& json_file_path)
{
    try
    {
        std::ifstream file(json_file_path);
        if (!file.is_open())
        {
            std::cerr << "Error: Cannot open credentials file: " << json_file_path << std::endl;
            return 1;
        }

        std::string json_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        cJSON* json = cJSON_Parse(json_content.c_str());
        if (!json)
        {
            std::cerr << "Error: Failed to parse JSON file" << std::endl;
            return 1;
        }

        if (!cJSON_IsArray(json))
        {
            std::cerr << "Error: JSON file must contain an array" << std::endl;
            cJSON_Delete(json);
            return 1;
        }

        pqxx::work txn(conn);
        int count = 0;
        int array_size = cJSON_GetArraySize(json);

        for (int i = 0; i < array_size; i++)
        {
            cJSON* item = cJSON_GetArrayItem(json, i);
            if (!item || !cJSON_IsObject(item))
            {
                continue;
            }

            cJSON* username_json = cJSON_GetObjectItemCaseSensitive(item, "username");
            cJSON* password_json = cJSON_GetObjectItemCaseSensitive(item, "password");
            cJSON* type_json = cJSON_GetObjectItemCaseSensitive(item, "type");

            if (!cJSON_IsString(username_json) || !cJSON_IsString(password_json) || !cJSON_IsString(type_json))
            {
                continue;
            }

            std::string username = username_json->valuestring;
            std::string password = password_json->valuestring;
            std::string client_type = type_json->valuestring;

            try
            {
                txn.exec(
                    pqxx::zview("INSERT INTO credentials (username, password_hash, client_type) VALUES ($1, $2, $3) "
                                "ON CONFLICT (username) DO UPDATE SET password_hash = EXCLUDED.password_hash, "
                                "client_type = EXCLUDED.client_type"),
                    pqxx::params{username, password, client_type});
                count++;
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Error inserting credential for " << username << ": " << ex.what() << std::endl;
            }
        }

        cJSON_Delete(json);
        txn.commit();
        std::cout << "Populated " << count << " credentials into database." << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error populating credentials table: " << ex.what() << std::endl;
        return 1;
    }
}
