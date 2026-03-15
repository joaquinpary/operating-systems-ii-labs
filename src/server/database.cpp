#include "database.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#define DEFAULT_DB_PORT 5432
#define INITIAL_STOCK_HUB 100
#define INITIAL_STOCK_WAREHOUSE 500

namespace
{
std::string require_env_var(const char* env_var)
{
    const char* value = std::getenv(env_var);
    if (value == nullptr || value[0] == '\0')
    {
        throw std::runtime_error(std::string("Missing required environment variable: ") + env_var);
    }
    return std::string(value);
}

std::string trim_copy(const std::string& value)
{
    const std::string whitespace = " \t\n\r";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return "";
    }
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

bool parse_client_conf_credentials(const std::string& conf_path, std::string& username, std::string& password,
                                   std::string& client_type)
{
    std::ifstream file(conf_path);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string key = trim_copy(line.substr(0, eq));
        const std::string value = trim_copy(line.substr(eq + 1));

        if (key == "username")
        {
            username = value;
        }
        else if (key == "password")
        {
            password = value;
        }
        else if (key == "type")
        {
            client_type = value;
        }
    }

    return !username.empty() && !password.empty() && !client_type.empty();
}
} // namespace

std::string build_connection_string()
{
    std::string host = require_env_var("POSTGRES_HOST");
    std::string db = require_env_var("POSTGRES_DB");
    std::string user = require_env_var("POSTGRES_USER");
    std::string password = require_env_var("POSTGRES_PASSWORD");

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
            std::cerr << "Invalid POSTGRES_PORT value. Falling back to " << DEFAULT_DB_PORT << std::endl;
        }
    }

    return "host=" + host + " dbname=" + db + " user=" + user + " password=" + password +
           " port=" + std::to_string(port);
}

std::unique_ptr<pqxx::connection> connect_to_database()
{
    try
    {
        std::string conn_string = build_connection_string();
        auto conn = std::make_unique<pqxx::connection>(conn_string);

        if (conn->is_open())
        {
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

int initialize_database(pqxx::connection& conn, const std::string& credentials_dir_path)
{
    try
    {
        if (create_credentials_table(conn) != 0)
        {
            std::cerr << "Failed to initialize database tables." << std::endl;
            return -1;
        }

        if (create_inventory_tables(conn) != 0)
        {
            std::cerr << "Failed to initialize inventory tables." << std::endl;
            return -1;
        }

        if (populate_credentials_table(conn, credentials_dir_path) != 0)
        {
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Database initialization error: " << ex.what() << std::endl;
        return -1;
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
                          "is_active BOOLEAN DEFAULT false"
                          ");";

        txn.exec(sql);
        txn.commit();
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

int populate_credentials_table(pqxx::connection& conn, const std::string& credentials_dir_path)
{
    try
    {
        namespace fs = std::filesystem;
        fs::path credentials_dir(credentials_dir_path);

        if (!fs::exists(credentials_dir) || !fs::is_directory(credentials_dir))
        {
            std::cerr << "Error: Credentials path is not a valid directory: " << credentials_dir_path << std::endl;
            return 1;
        }

        pqxx::work txn(conn);
        int count = 0;
        int conf_count = 0;

        for (const auto& entry : fs::directory_iterator(credentials_dir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".conf")
            {
                continue;
            }
            conf_count++;

            std::string username;
            std::string password;
            std::string client_type;
            if (!parse_client_conf_credentials(entry.path().string(), username, password, client_type))
            {
                std::cerr << "Error: Invalid client credentials in file: " << entry.path() << std::endl;
                continue;
            }

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

        if (conf_count > 0 && count == 0)
        {
            std::cerr << "Error: No valid credentials found in credentials directory" << std::endl;
            return 1;
        }

        txn.commit();
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
            return "";
        }

        std::string warehouse_id = result[0][0].as<std::string>();
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

int get_transaction_by_id(pqxx::connection& conn, int transaction_id, transaction_record& out)
{
    try
    {
        pqxx::work txn(conn);
        std::string sql = "SELECT transaction_id, transaction_type, source_id, source_type, destination_id, "
                          "destination_type, status, food, water, medicine, tools, guns, ammo FROM "
                          "inventory_transactions WHERE transaction_id = $1";

        pqxx::result result = txn.exec(pqxx::zview(sql), pqxx::params{transaction_id});

        if (result.empty())
        {
            return -1;
        }

        const auto& row = result[0];
        out.transaction_id = row[0].as<int>();
        out.transaction_type = row[1].as<std::string>();
        out.source_id = row[2].is_null() ? "" : row[2].as<std::string>();
        out.source_type = row[3].is_null() ? "" : row[3].as<std::string>();
        out.destination_id = row[4].is_null() ? "" : row[4].as<std::string>();
        out.destination_type = row[5].is_null() ? "" : row[5].as<std::string>();
        out.status = row[6].as<std::string>();
        out.food = row[7].as<int>();
        out.water = row[8].as<int>();
        out.medicine = row[9].as<int>();
        out.tools = row[10].as<int>();
        out.guns = row[11].as<int>();
        out.ammo = row[12].as<int>();

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error getting transaction by id: " << ex.what() << std::endl;
        return -1;
    }
}

int get_client_inventory(pqxx::connection& conn, const std::string& client_id, const std::string& client_type,
                         int quantities_out[6])
{
    if (client_id.empty() || client_type.empty() || !quantities_out)
    {
        std::cerr << "Invalid parameters for get_client_inventory" << std::endl;
        return -1;
    }

    // Initialize to default stock based on role (warehouse gets more stock than hub)
    int initial_stock = (client_type == "WAREHOUSE") ? INITIAL_STOCK_WAREHOUSE : INITIAL_STOCK_HUB;
    for (int i = 0; i < 6; i++)
    {
        quantities_out[i] = initial_stock;
    }

    try
    {
        pqxx::work txn(conn);
        std::string sql = "SELECT food, water, medicine, tools, guns, ammo FROM client_inventory WHERE client_id = $1";

        pqxx::result result = txn.exec(pqxx::zview(sql), pqxx::params{client_id});

        if (result.empty())
        {
            std::string insert_sql =
                "INSERT INTO client_inventory (client_id, client_type, food, water, medicine, tools, guns, ammo) "
                "VALUES ($1, $2, $3, $3, $3, $3, $3, $3)";
            txn.exec(pqxx::zview(insert_sql), pqxx::params{client_id, client_type, initial_stock});
            txn.commit();
            // quantities_out already initialized to initial_stock above
            return 0;
        }

        quantities_out[0] = result[0][0].as<int>();
        quantities_out[1] = result[0][1].as<int>();
        quantities_out[2] = result[0][2].as<int>();
        quantities_out[3] = result[0][3].as<int>();
        quantities_out[4] = result[0][4].as<int>();
        quantities_out[5] = result[0][5].as<int>();

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error getting client inventory: " << ex.what() << std::endl;
        return -1;
    }
}

int adjust_client_inventory(pqxx::connection& conn, const std::string& client_id, const int quantities[6], bool add)
{
    if (client_id.empty() || !quantities)
    {
        std::cerr << "Invalid parameters for adjust_client_inventory" << std::endl;
        return -1;
    }

    try
    {
        pqxx::work txn(conn);

        std::string sql;
        if (add)
        {
            sql = "UPDATE client_inventory SET "
                  "food = food + $2, water = water + $3, medicine = medicine + $4, "
                  "tools = tools + $5, guns = guns + $6, ammo = ammo + $7, "
                  "last_updated = CURRENT_TIMESTAMP "
                  "WHERE client_id = $1";
        }
        else
        {
            sql = "UPDATE client_inventory SET "
                  "food = GREATEST(0, food - $2), water = GREATEST(0, water - $3), "
                  "medicine = GREATEST(0, medicine - $4), tools = GREATEST(0, tools - $5), "
                  "guns = GREATEST(0, guns - $6), ammo = GREATEST(0, ammo - $7), "
                  "last_updated = CURRENT_TIMESTAMP "
                  "WHERE client_id = $1";
        }

        txn.exec(pqxx::zview(sql), pqxx::params{client_id, quantities[0], quantities[1], quantities[2], quantities[3],
                                                quantities[4], quantities[5]});
        txn.commit();

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error adjusting client inventory: " << ex.what() << std::endl;
        return -1;
    }
}

int set_client_active(pqxx::connection& conn, const std::string& username, bool active)
{
    if (username.empty())
    {
        std::cerr << "Invalid parameters for set_client_active" << std::endl;
        return -1;
    }

    try
    {
        pqxx::work txn(conn);
        std::string sql = "UPDATE credentials SET is_active = $1 WHERE username = $2";
        txn.exec(pqxx::zview(sql), pqxx::params{active, username});
        txn.commit();

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error setting client active status: " << ex.what() << std::endl;
        return -1;
    }
}

int reset_all_clients_inactive(pqxx::connection& conn)
{
    try
    {
        pqxx::work txn(conn);
        txn.exec("UPDATE credentials SET is_active = false");
        txn.commit();

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error resetting clients to inactive: " << ex.what() << std::endl;
        return -1;
    }
}
