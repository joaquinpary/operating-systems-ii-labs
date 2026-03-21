#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <memory>
#include <pqxx/pqxx>
#include <string>

/** Credential row loaded from the server credentials table. */
struct credential
{
    int id;                    ///< Database identifier.
    std::string username;      ///< Unique username.
    std::string password_hash; ///< Stored password hash.
    std::string client_type;   ///< Client role such as HUB or WAREHOUSE.
    bool is_active;            ///< Whether the client is currently active.
};

/** Inventory transaction row used by inventory workflows and tests. */
struct transaction_record
{
    int transaction_id;           ///< Primary key of the transaction.
    std::string transaction_type; ///< STOCK_REQUEST, REPLENISH, etc.
    std::string source_id;        ///< Current source client identifier.
    std::string source_type;      ///< Source client type.
    std::string destination_id;   ///< Destination client identifier.
    std::string destination_type; ///< Destination client type.
    std::string status;           ///< Current workflow status.
    int food;                     ///< Requested or moved food quantity.
    int water;                    ///< Requested or moved water quantity.
    int medicine;                 ///< Requested or moved medicine quantity.
    int tools;                    ///< Requested or moved tools quantity.
    int guns;                     ///< Requested or moved guns quantity.
    int ammo;                     ///< Requested or moved ammo quantity.
};

/** Build the PostgreSQL connection string from environment variables. */
std::string build_connection_string();
/** Create a database connection, returning nullptr when configuration or connection fails. */
std::unique_ptr<pqxx::connection> connect_to_database();
/** Initialize server tables and load credentials from a directory. */
int initialize_database(pqxx::connection& conn, const std::string& credentials_dir_path);
/** Ensure the credentials table exists. */
int create_credentials_table(pqxx::connection& conn);
/** Query one credential row by username. */
std::unique_ptr<credential> query_credentials_by_username(pqxx::connection& conn, const std::string& username);
/** Populate the credentials table from the configured credential directory. */
int populate_credentials_table(pqxx::connection& conn, const std::string& credentials_dir_path);

/** Ensure all inventory-related tables exist. */
int create_inventory_tables(pqxx::connection& conn);

/**
 * Self-committing helpers.
 * Each function creates its own pqxx::work and commits on success.
 */
/** Upsert a client's inventory snapshot. */
int update_client_inventory(pqxx::connection& conn, const std::string& client_id, const std::string& client_type,
                            const int quantities[6], const std::string& timestamp);
/** Return a warehouse that can fulfill all requested quantities, or an empty string. */
std::string get_warehouse_with_all_stock(pqxx::connection& conn, const int quantities[6]);
/** Create a new inventory transaction row. */
int create_transaction(pqxx::connection& conn, const std::string& transaction_type, const std::string& destination_id,
                       const std::string& destination_type, const int quantities[6],
                       const std::string& order_timestamp);
/** Assign a destination client to an existing transaction. */
int set_transaction_destination(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                                const std::string& client_type);
/** Assign a source client to an existing transaction. */
int set_transaction_source(pqxx::connection& conn, int transaction_id, const std::string& client_id,
                           const std::string& client_type);
/** Mark a transaction as dispatched and persist its dispatch timestamp. */
int mark_transaction_dispatched(pqxx::connection& conn, int transaction_id, const std::string& dispatch_timestamp);
/** Mark a transaction as assigned to a warehouse. */
int mark_transaction_assigned(pqxx::connection& conn, int transaction_id);
/** Mark a transaction as completed and persist the reception timestamp. */
int complete_transaction(pqxx::connection& conn, int transaction_id, const std::string& reception_timestamp);
/** Load pending transactions into a caller-provided buffer. */
int get_pending_transactions(pqxx::connection& conn, transaction_record* out_transactions, int max_count);
/** Find a transaction by source, destination and current status. */
int find_transaction_id(pqxx::connection& conn, const std::string& source_id, const std::string& destination_id,
                        const std::string& status);
/** Load a transaction row by identifier. */
int get_transaction_by_id(pqxx::connection& conn, int transaction_id, transaction_record& out);
/** Read the current inventory for a client. */
int get_client_inventory(pqxx::connection& conn, const std::string& client_id, const std::string& client_type,
                         int quantities_out[6]);
/** Add or subtract quantities from the stored inventory of a client. */
int adjust_client_inventory(pqxx::connection& conn, const std::string& client_id, const int quantities[6], bool add);

/**
 * Transaction-aware helpers.
 * The caller owns the pqxx::work and can batch several operations atomically.
 */
/** Upsert a client's inventory snapshot inside an existing SQL transaction. */
int update_client_inventory(pqxx::work& txn, const std::string& client_id, const std::string& client_type,
                            const int quantities[6], const std::string& timestamp);
/** Return a warehouse that can fulfill all requested quantities within an existing transaction. */
std::string get_warehouse_with_all_stock(pqxx::work& txn, const int quantities[6]);
/** Create a new inventory transaction row inside an existing transaction. */
int create_transaction(pqxx::work& txn, const std::string& transaction_type, const std::string& destination_id,
                       const std::string& destination_type, const int quantities[6],
                       const std::string& order_timestamp);
/** Assign a source client to an existing transaction inside an existing transaction. */
int set_transaction_source(pqxx::work& txn, int transaction_id, const std::string& client_id,
                           const std::string& client_type);
/** Mark a transaction as dispatched inside an existing transaction. */
int mark_transaction_dispatched(pqxx::work& txn, int transaction_id, const std::string& dispatch_timestamp);
/** Mark a transaction as assigned inside an existing transaction. */
int mark_transaction_assigned(pqxx::work& txn, int transaction_id);
/** Mark a transaction as completed inside an existing transaction. */
int complete_transaction(pqxx::work& txn, int transaction_id, const std::string& reception_timestamp);
/** Load pending transactions into a caller-provided buffer inside an existing transaction. */
int get_pending_transactions(pqxx::work& txn, transaction_record* out_transactions, int max_count);
/** Find a transaction by source, destination and status inside an existing transaction. */
int find_transaction_id(pqxx::work& txn, const std::string& source_id, const std::string& destination_id,
                        const std::string& status);
/** Load a transaction row by identifier inside an existing transaction. */
int get_transaction_by_id(pqxx::work& txn, int transaction_id, transaction_record& out);
/** Read the current inventory for a client inside an existing transaction. */
int get_client_inventory(pqxx::work& txn, const std::string& client_id, const std::string& client_type,
                         int quantities_out[6]);
/** Add or subtract quantities from stored inventory inside an existing transaction. */
int adjust_client_inventory(pqxx::work& txn, const std::string& client_id, const int quantities[6], bool add);

/** Update a client's active flag in the credentials table. */
int set_client_active(pqxx::connection& conn, const std::string& username, bool active);

/** Reset all clients to inactive, typically during server startup. */
int reset_all_clients_inactive(pqxx::connection& conn);

#endif
