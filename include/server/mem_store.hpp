#ifndef MEM_STORE_HPP
#define MEM_STORE_HPP

#include "database.hpp"

#include <array>
#include <mutex>
#include <pthread.h>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class connection_pool;

/**
 * In-memory store ("redis casero") backed by POSIX rwlocks.
 *
 * Three tables:
 *   1. Credentials  — loaded once at startup, read-only except is_active toggle.
 *   2. Inventory    — client_id → int[6], write-through to DB.
 *   3. Transactions — transaction_id → record, with secondary indices.
 *
 * All public methods are thread-safe.
 * Write operations persist to the database synchronously (write-through).
 */
class mem_store
{
  public:
    explicit mem_store(connection_pool& pool);
    ~mem_store() = default;

    // Non-copyable, non-movable
    mem_store(const mem_store&) = delete;
    mem_store& operator=(const mem_store&) = delete;

    // ======================== INIT ========================
    // Preload all credentials from DB (call once at startup)
    void load_credentials();

    // ======================== CREDENTIALS ========================
    // Lookup credential by username. Returns nullptr if not found.
    std::unique_ptr<credential> get_credential(const std::string& username);

    // Mark client active/inactive in cache AND DB (write-through)
    void set_active(const std::string& username, bool active);

    // Reset all clients to inactive (startup)
    void reset_all_inactive();

    // ======================== INVENTORY ========================
    // Get cached inventory for a client. Returns true if found (fills quantities_out).
    // If not in cache, loads from DB and caches it.
    bool get_inventory(const std::string& client_id, const std::string& client_type, int quantities_out[6]);

    // Set/replace inventory in cache AND DB (write-through).
    void update_inventory(const std::string& client_id, const std::string& client_type, const int quantities[6],
                          const std::string& timestamp);

    // Adjust inventory in cache (add/subtract) AND DB (write-through).
    // Returns 0 on success.
    int adjust_inventory(const std::string& client_id, const int quantities[6], bool add);

    // Find a warehouse with sufficient stock for ALL requested quantities.
    // Scans the inventory cache (no DB hit). Returns empty string if none found.
    std::string find_warehouse_with_stock(const int quantities[6]);

    // ======================== TRANSACTIONS ========================
    // Create transaction in cache AND DB. Returns transaction_id or -1.
    int create_transaction(const std::string& transaction_type, const std::string& destination_id,
                           const std::string& destination_type, const int quantities[6],
                           const std::string& order_timestamp);

    // Set transaction source in cache AND DB.
    int set_transaction_source(int transaction_id, const std::string& client_id, const std::string& client_type);

    // Mark transaction ASSIGNED in cache AND DB.
    int mark_transaction_assigned(int transaction_id);

    // Mark transaction DISPATCHED in cache AND DB.
    int mark_transaction_dispatched(int transaction_id, const std::string& dispatch_timestamp);

    // Complete transaction in cache AND DB.
    int complete_transaction(int transaction_id, const std::string& reception_timestamp);

    // Find a transaction by source/destination/status (cache-only scan).
    int find_transaction(const std::string& source_id, const std::string& destination_id, const std::string& status);

    // Get transaction record by id (cache lookup).
    int get_transaction(int transaction_id, transaction_record& out);

    // Get all pending transactions (status == "PENDING") from cache.
    int get_pending_transactions(transaction_record* out, int max_count);

  private:
    connection_pool& m_pool;

    // --- Credentials table (read-heavy, write-rare) ---
    std::shared_mutex m_cred_mtx;
    std::unordered_map<std::string, credential> m_credentials; // username → credential
    std::unordered_set<std::string> m_active_clients;          // usernames currently active

    // --- Inventory table (read/write) ---
    std::shared_mutex m_inv_mtx;
    // client_id → [food, water, medicine, tools, guns, ammo]
    std::unordered_map<std::string, std::array<int, 6>> m_inventory;
    // Which client_ids are warehouses (for stock scanning)
    std::unordered_set<std::string> m_warehouse_ids;

    // --- Transactions table (read/write) ---
    std::shared_mutex m_txn_mtx;
    std::unordered_map<int, transaction_record> m_transactions; // txn_id → record

    // Helper: update secondary indices after modifying a transaction
    void txn_update_record(const transaction_record& rec);
};

#endif // MEM_STORE_HPP
