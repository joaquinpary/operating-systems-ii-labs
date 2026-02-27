#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <memory>
#include <pqxx/pqxx>
#include <string>

struct credential
{
    int id;
    std::string username;
    std::string password_hash;
    std::string client_type;
    bool is_active;
};

struct transaction_record
{
    int transaction_id;
    std::string transaction_type;
    std::string source_id;
    std::string source_type;
    std::string destination_id;
    std::string destination_type;
    std::string status;
    int food;
    int water;
    int medicine;
    int tools;
    int guns;
    int ammo;
};

std::unique_ptr<pqxx::connection> connect_to_database();
std::unique_ptr<pqxx::connection> initialize_database();
int create_credentials_table(pqxx::connection& conn);
std::unique_ptr<credential> query_credentials_by_username(pqxx::connection& conn, const std::string& username);
int populate_credentials_table(pqxx::connection& conn, const std::string& json_file_path);

// Inventory management functions
int create_inventory_tables(pqxx::connection& conn);
int update_client_inventory(pqxx::connection& conn, const std::string& client_id, const std::string& client_type,
                            const int quantities[6], const std::string& timestamp);
std::string get_warehouse_with_all_stock(pqxx::connection& conn, const int quantities[6]);
int create_transaction(pqxx::connection& conn, const std::string& transaction_type, const std::string& destination_id,
                       const std::string& destination_type, const int quantities[6],
                       const std::string& order_timestamp);
int set_transaction_destination(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                                const std::string& client_type);
int set_transaction_source(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                           const std::string& client_type);
int mark_transaction_dispatched(pqxx::connection& conn, int transaction_id, const std::string& dispatch_timestamp);
int mark_transaction_assigned(pqxx::connection& conn, int transaction_id);
int complete_transaction(pqxx::connection& conn, int transaction_id, const std::string& reception_timestamp);
int get_pending_transactions(pqxx::connection& conn, transaction_record* out_transactions, int max_count);
int find_transaction_id(pqxx::connection& conn, const std::string& source_id, const std::string& destination_id,
                        const std::string& status);

// Query a client's current inventory from the database
// Returns 0 on success (quantities_out populated), -1 on error
// If client has no inventory row, quantities_out is filled with zeros
int get_client_inventory(pqxx::connection& conn, const std::string& client_id, int quantities_out[6]);

#endif
