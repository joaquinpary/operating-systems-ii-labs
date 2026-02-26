#include "database.hpp"

#include <cJSON.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define CREDENTIALS_PATH "config/credentials.json"
#define CLIENT_CREDENTIALS_PATH "CLIENT_CREDENTIALS_PATH"

// Anonymous namespace - everything here has internal linkage (visible only in this file)
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
        if (create_credentials_table(*conn) != 0)
        {
            std::cerr << "Failed to initialize database tables." << std::endl;
            return nullptr;
        }

        // Create inventory tables
        if (create_inventory_tables(*conn) != 0)
        {
            std::cerr << "Failed to initialize inventory tables." << std::endl;
            return nullptr;
        }

        // Populate credentials table from JSON file if it exists
        const std::string credentials_file = get_env_var(CLIENT_CREDENTIALS_PATH, CREDENTIALS_PATH);
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

int create_credentials_table(pqxx::connection& conn)
{
    try
    {
        pqxx::work txn(conn);
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

std::unique_ptr<credential> query_credentials_by_username(pqxx::connection& conn, const std::string& username)
{
    try
    {
        pqxx::work txn(conn);
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

        // If array has items but none were valid, return error
        if (array_size > 0 && count == 0)
        {
            std::cerr << "Error: No valid credentials found in file" << std::endl;
            return 1;
        }

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

int create_inventory_tables(pqxx::connection& conn)
{
    try
    {
        pqxx::work txn(conn);
        // Create client_inventory table - one row per client with all 6 items
        std::string sql_inventory = "CREATE TABLE IF NOT EXISTS client_inventory ("
                                    "client_id TEXT PRIMARY KEY, "
                                    "client_type TEXT NOT NULL, "
                                    "food INTEGER NOT NULL DEFAULT 0, "
                                    "water INTEGER NOT NULL DEFAULT 0, "
                                    "medicine INTEGER NOT NULL DEFAULT 0, "
                                    "tools INTEGER NOT NULL DEFAULT 0, "
                                    "guns INTEGER NOT NULL DEFAULT 0, "
                                    "ammo INTEGER NOT NULL DEFAULT 0, "
                                    "last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                                    ");";

        txn.exec(sql_inventory);
        std::cout << "client_inventory table created successfully." << std::endl;

        // Create inventory_transactions table - audit trail with embedded items
        std::string sql_transactions = "CREATE TABLE IF NOT EXISTS inventory_transactions ("
                                       "transaction_id SERIAL PRIMARY KEY, "
                                       "transaction_type TEXT NOT NULL, "
                                       "source_id TEXT, "
                                       "source_type TEXT, "
                                       "destination_id TEXT, "
                                       "destination_type TEXT, "
                                       "status TEXT DEFAULT 'PENDING', "
                                       "food INTEGER NOT NULL DEFAULT 0, "
                                       "water INTEGER NOT NULL DEFAULT 0, "
                                       "medicine INTEGER NOT NULL DEFAULT 0, "
                                       "tools INTEGER NOT NULL DEFAULT 0, "
                                       "guns INTEGER NOT NULL DEFAULT 0, "
                                       "ammo INTEGER NOT NULL DEFAULT 0, "
                                       "order_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
                                       "dispatch_timestamp TIMESTAMP, "
                                       "reception_timestamp TIMESTAMP"
                                       ");";

        txn.exec(sql_transactions);
        std::cout << "inventory_transactions table created successfully." << std::endl;

        txn.commit();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error creating inventory tables: " << ex.what() << std::endl;
        return 1;
    }
}

int update_client_inventory(pqxx::connection& conn, const std::string& client_id, const std::string& client_type,
                            const int quantities[6], const std::string& timestamp)
{
    if (client_id.empty() || client_type.empty())
    {
        std::cerr << "Invalid parameters for update_client_inventory" << std::endl;
        return -1;
    }

    try
    {
        pqxx::work txn(conn);

        // quantities array: [food, water, medicine, tools, guns, ammo]
        int food = quantities[0];
        int water = quantities[1];
        int medicine = quantities[2];
        int tools = quantities[3];
        int guns = quantities[4];
        int ammo = quantities[5];

        std::string sql = "INSERT INTO client_inventory (client_id, client_type, food, water, medicine, tools, guns, "
                          "ammo, last_updated) "
                          "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
                          "ON CONFLICT (client_id) DO UPDATE SET "
                          "client_type = EXCLUDED.client_type, "
                          "food = EXCLUDED.food, "
                          "water = EXCLUDED.water, "
                          "medicine = EXCLUDED.medicine, "
                          "tools = EXCLUDED.tools, "
                          "guns = EXCLUDED.guns, "
                          "ammo = EXCLUDED.ammo, "
                          "last_updated = EXCLUDED.last_updated";

        txn.exec(pqxx::zview(sql),
                 pqxx::params{client_id, client_type, food, water, medicine, tools, guns, ammo, timestamp});
        txn.commit();

        std::cout << "Updated inventory for client " << client_id << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error updating client inventory: " << ex.what() << std::endl;
        return -1;
    }
}

std::string get_warehouse_with_all_stock(pqxx::connection& conn, const int quantities[6])
{
    if (!quantities)
    {
        return "";
    }

    try
    {
        pqxx::work txn(conn);
        // Find warehouses with sufficient stock for ALL requested items
        // quantities array: [food, water, medicine, tools, guns, ammo]
        std::string sql = "SELECT client_id FROM client_inventory "
                          "WHERE client_type = 'WAREHOUSE' "
                          "AND food >= $1 AND water >= $2 AND medicine >= $3 "
                          "AND tools >= $4 AND guns >= $5 AND ammo >= $6 "
                          "ORDER BY RANDOM() LIMIT 1";

        pqxx::result result = txn.exec(pqxx::zview(sql), pqxx::params{quantities[0], quantities[1], quantities[2],
                                                                      quantities[3], quantities[4], quantities[5]});

        if (result.empty())
        {
            std::cout << "No warehouse found with sufficient stock for all items" << std::endl;
            return "";
        }

        std::string warehouse_id = result[0][0].as<std::string>();
        std::cout << "Found warehouse " << warehouse_id << " with sufficient stock" << std::endl;
        return warehouse_id;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error finding warehouse with stock: " << ex.what() << std::endl;
        return "";
    }
}

int create_transaction(pqxx::connection& conn, const std::string& transaction_type, const std::string& destination_id,
                       const std::string& destination_type, const int quantities[6], const std::string& order_timestamp)
{
    if (transaction_type.empty() || destination_id.empty() || destination_type.empty())
    {
        std::cerr << "Invalid parameters for create_transaction" << std::endl;
        return -1;
    }

    try
    {
        pqxx::work txn(conn);

        // quantities array: [food, water, medicine, tools, guns, ammo]
        int food = quantities[0];
        int water = quantities[1];
        int medicine = quantities[2];
        int tools = quantities[3];
        int guns = quantities[4];
        int ammo = quantities[5];

        std::string sql = "INSERT INTO inventory_transactions (transaction_type, destination_id, destination_type, "
                          "food, water, medicine, "
                          "tools, guns, ammo, order_timestamp) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) "
                          "RETURNING transaction_id";

        pqxx::result result =
            txn.exec(pqxx::zview(sql), pqxx::params{transaction_type, destination_id, destination_type, food, water,
                                                    medicine, tools, guns, ammo, order_timestamp});

        int transaction_id = result[0][0].as<int>();
        txn.commit();

        std::cout << "Created transaction " << transaction_id << " for " << destination_id << " (" << destination_type
                  << ")" << std::endl;
        return transaction_id;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error creating transaction: " << ex.what() << std::endl;
        return -1;
    }
}

// is this set_transaction_destination needed? maybe we can just set the destination when we create the transaction
// because the transaction is created when a hub or warehouse requests stock, so we already know the destination at that
// point

int set_transaction_destination(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                                const std::string& client_type)
{
    try
    {
        pqxx::work txn(conn);

        std::string sql = "UPDATE inventory_transactions SET destination_id = $1, destination_type = $2 "
                          "WHERE transaction_id = $3";

        txn.exec(pqxx::zview(sql), pqxx::params{client_id, client_type, transaction_id});
        txn.commit();

        std::cout << "Set destination " << client_id << " (" << client_type << ") for transaction " << transaction_id
                  << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error setting transaction destination: " << ex.what() << std::endl;
        return -1;
    }
}

int set_transaction_source(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                           const std::string& client_type)
{
    try
    {
        pqxx::work txn(conn);

        std::string sql = "UPDATE inventory_transactions SET source_id = $1, source_type = $2 "
                          "WHERE transaction_id = $3";

        txn.exec(pqxx::zview(sql), pqxx::params{client_id, client_type, transaction_id});
        txn.commit();

        std::cout << "Set source " << client_id << " (" << client_type << ") for transaction " << transaction_id
                  << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error setting transaction source: " << ex.what() << std::endl;
        return -1;
    }
}

int mark_transaction_dispatched(pqxx::connection& conn, int transaction_id, const std::string& dispatch_timestamp)
{
    try
    {
        pqxx::work txn(conn);

        std::string sql = "UPDATE inventory_transactions SET status = 'DISPATCHED', dispatch_timestamp = $1 WHERE "
                          "transaction_id = $2";

        txn.exec(pqxx::zview(sql), pqxx::params{dispatch_timestamp, transaction_id});
        txn.commit();

        std::cout << "Marked transaction " << transaction_id << " as dispatched" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error marking transaction as dispatched: " << ex.what() << std::endl;
        return -1;
    }
}

int mark_transaction_assigned(pqxx::connection& conn, int transaction_id)
{
    try
    {
        pqxx::work txn(conn);

        std::string sql = "UPDATE inventory_transactions SET status = 'ASSIGNED' WHERE transaction_id = $1";

        txn.exec(pqxx::zview(sql), pqxx::params{transaction_id});
        txn.commit();

        std::cout << "Marked transaction " << transaction_id << " as assigned" << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error marking transaction as assigned: " << ex.what() << std::endl;
        return -1;
    }
}

int complete_transaction(pqxx::connection& conn, int transaction_id, const std::string& reception_timestamp)
{
    try
    {
        pqxx::work txn(conn);

        std::string sql = "UPDATE inventory_transactions SET status = 'COMPLETED', reception_timestamp = $1 WHERE "
                          "transaction_id = $2";

        txn.exec(pqxx::zview(sql), pqxx::params{reception_timestamp, transaction_id});
        txn.commit();

        std::cout << "Completed transaction " << transaction_id << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error completing transaction: " << ex.what() << std::endl;
        return -1;
    }
}

int get_pending_transactions(pqxx::connection& conn, transaction_record* out_transactions, int max_count)
{
    if (!out_transactions || max_count <= 0)
    {
        std::cerr << "Invalid parameters for get_pending_transactions" << std::endl;
        return 0;
    }

    try
    {
        pqxx::work txn(conn);
        std::string sql = "SELECT transaction_id, transaction_type, source_id, source_type, destination_id, "
                          "destination_type, status, food, water, medicine, tools, guns, ammo FROM "
                          "inventory_transactions WHERE status = 'PENDING' ORDER BY order_timestamp ASC";

        pqxx::result result = txn.exec(sql);

        int count = 0;
        for (const auto& row : result)
        {
            if (count >= max_count)
                break;

            out_transactions[count].transaction_id = row[0].as<int>();
            out_transactions[count].transaction_type = row[1].as<std::string>();
            out_transactions[count].source_id = row[2].is_null() ? "" : row[2].as<std::string>();
            out_transactions[count].source_type = row[3].is_null() ? "" : row[3].as<std::string>();
            out_transactions[count].destination_id = row[4].is_null() ? "" : row[4].as<std::string>();
            out_transactions[count].destination_type = row[5].is_null() ? "" : row[5].as<std::string>();
            out_transactions[count].status = row[6].as<std::string>();
            out_transactions[count].food = row[7].as<int>();
            out_transactions[count].water = row[8].as<int>();
            out_transactions[count].medicine = row[9].as<int>();
            out_transactions[count].tools = row[10].as<int>();
            out_transactions[count].guns = row[11].as<int>();
            out_transactions[count].ammo = row[12].as<int>();

            count++;
        }

        std::cout << "Found " << count << " pending transactions" << std::endl;
        return count;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error getting pending transactions: " << ex.what() << std::endl;
        return 0;
    }
}

int find_transaction_id(pqxx::connection& conn, const std::string& source_id, const std::string& destination_id,
                        const std::string& status)
{
    try
    {
        pqxx::work txn(conn);
        std::string sql = "SELECT transaction_id FROM inventory_transactions WHERE 1=1";

        if (!source_id.empty())
            sql += " AND source_id = " + txn.quote(source_id);
        if (!destination_id.empty())
            sql += " AND destination_id = " + txn.quote(destination_id);
        if (!status.empty())
            sql += " AND status = " + txn.quote(status);

        sql += " ORDER BY order_timestamp DESC LIMIT 1";

        pqxx::result result = txn.exec(sql);

        if (result.empty())
        {
            return -1;
        }

        return result[0][0].as<int>();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error finding transaction_id: " << ex.what() << std::endl;
        return -1;
    }
}
