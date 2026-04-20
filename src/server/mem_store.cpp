#include "mem_store.hpp"
#include "connection_pool.hpp"

#include <common/json_manager.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>

static constexpr int INITIAL_STOCK_HUB = 100;
static constexpr int INITIAL_STOCK_WAREHOUSE = 500;

mem_store::mem_store(connection_pool& pool) : m_pool(pool)
{
}

void mem_store::load_credentials()
{
    auto guard = m_pool.acquire();
    try
    {
        pqxx::work txn(guard.get());
        pqxx::result rows = txn.exec("SELECT id, username, password_hash, client_type, is_active FROM credentials");

        std::unique_lock lock(m_cred_mtx);
        m_credentials.clear();
        m_active_clients.clear();

        for (const auto& row : rows)
        {
            credential cred;
            cred.id = row[0].as<int>();
            cred.username = row[1].as<std::string>();
            cred.password_hash = row[2].as<std::string>();
            cred.client_type = row[3].as<std::string>();
            cred.is_active = row[4].as<bool>();

            if (cred.is_active)
                m_active_clients.insert(cred.username);

            if (cred.client_type == "WAREHOUSE")
            {
                std::unique_lock inv_lock(m_inv_mtx);
                m_warehouse_ids.insert(cred.username);
            }

            m_credentials[cred.username] = std::move(cred);
        }

        std::cerr << "[MEM_STORE] Loaded " << m_credentials.size() << " credentials (" << m_warehouse_ids.size()
                  << " warehouses)" << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error loading credentials: " << ex.what() << std::endl;
    }
}

std::unique_ptr<credential> mem_store::get_credential(const std::string& username)
{
    std::shared_lock lock(m_cred_mtx);
    auto it = m_credentials.find(username);
    if (it == m_credentials.end())
        return nullptr;

    auto cred = std::make_unique<credential>(it->second);
    cred->is_active = m_active_clients.count(username) > 0;
    return cred;
}

void mem_store::set_active(const std::string& username, bool active)
{
    {
        std::unique_lock lock(m_cred_mtx);
        if (active)
            m_active_clients.insert(username);
        else
            m_active_clients.erase(username);
    }

    try
    {
        auto guard = m_pool.acquire();
        ::set_client_active(guard.get(), username, active);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting active status for " << username << ": " << ex.what() << std::endl;
    }
}

void mem_store::reset_all_inactive()
{
    {
        std::unique_lock lock(m_cred_mtx);
        m_active_clients.clear();
    }

    try
    {
        auto guard = m_pool.acquire();
        ::reset_all_clients_inactive(guard.get());
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error resetting all inactive: " << ex.what() << std::endl;
    }
}

bool mem_store::get_inventory(const std::string& client_id, const std::string& client_type, int quantities_out[6])
{
    {
        std::shared_lock lock(m_inv_mtx);
        auto it = m_inventory.find(client_id);
        if (it != m_inventory.end())
        {
            std::copy(it->second.begin(), it->second.end(), quantities_out);
            return true;
        }
    }

    try
    {
        auto guard = m_pool.acquire();
        if (::get_client_inventory(guard.get(), client_id, client_type, quantities_out) != 0)
            return false;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error loading inventory for " << client_id << ": " << ex.what() << std::endl;
        return false;
    }

    {
        std::unique_lock lock(m_inv_mtx);
        std::array<int, QUANTITY_ITEMS> arr;
        std::copy(quantities_out, quantities_out + QUANTITY_ITEMS, arr.begin());
        m_inventory[client_id] = arr;
        if (client_type == "WAREHOUSE")
            m_warehouse_ids.insert(client_id);
    }

    return true;
}

void mem_store::update_inventory(const std::string& client_id, const std::string& client_type, const int quantities[6],
                                 const std::string& timestamp)
{
    {
        std::unique_lock lock(m_inv_mtx);
        std::array<int, QUANTITY_ITEMS> arr;
        std::copy(quantities, quantities + QUANTITY_ITEMS, arr.begin());
        m_inventory[client_id] = arr;
        if (client_type == "WAREHOUSE")
            m_warehouse_ids.insert(client_id);
    }

    try
    {
        auto guard = m_pool.acquire();
        ::update_client_inventory(guard.get(), client_id, client_type, quantities, timestamp);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting inventory for " << client_id << ": " << ex.what() << std::endl;
    }
}

int mem_store::adjust_inventory(const std::string& client_id, const int quantities[6], bool add)
{
    {
        std::unique_lock lock(m_inv_mtx);
        auto it = m_inventory.find(client_id);
        if (it != m_inventory.end())
        {
            for (int i = 0; i < QUANTITY_ITEMS; i++)
            {
                if (add)
                    it->second[i] += quantities[i];
                else
                    it->second[i] = std::max(0, it->second[i] - quantities[i]);
            }
        }
    }

    try
    {
        auto guard = m_pool.acquire();
        return ::adjust_client_inventory(guard.get(), client_id, quantities, add);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting adjust_inventory for " << client_id << ": " << ex.what()
                  << std::endl;
        return -1;
    }
}

std::string mem_store::find_warehouse_with_stock(const int quantities[6])
{
    std::shared_lock lock(m_inv_mtx);

    std::vector<std::string> candidates;
    for (const auto& wh_id : m_warehouse_ids)
    {
        auto it = m_inventory.find(wh_id);
        if (it == m_inventory.end())
            continue;

        bool sufficient = true;
        for (int i = 0; i < QUANTITY_ITEMS; i++)
        {
            if (it->second[i] < quantities[i])
            {
                sufficient = false;
                break;
            }
        }
        if (sufficient)
            candidates.push_back(wh_id);
    }

    if (candidates.empty())
        return "";

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

int mem_store::create_transaction(const std::string& transaction_type, const std::string& destination_id,
                                  const std::string& destination_type, const int quantities[6],
                                  const std::string& order_timestamp)
{
    int transaction_id;
    try
    {
        auto guard = m_pool.acquire();
        transaction_id = ::create_transaction(guard.get(), transaction_type, destination_id, destination_type,
                                              quantities, order_timestamp);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error creating transaction in DB: " << ex.what() << std::endl;
        return -1;
    }

    if (transaction_id < 0)
        return -1;

    transaction_record rec;
    rec.transaction_id = transaction_id;
    rec.transaction_type = transaction_type;
    rec.source_id = "";
    rec.source_type = "";
    rec.destination_id = destination_id;
    rec.destination_type = destination_type;
    rec.status = "PENDING";
    rec.food = quantities[0];
    rec.water = quantities[1];
    rec.medicine = quantities[2];
    rec.tools = quantities[3];
    rec.guns = quantities[4];
    rec.ammo = quantities[5];

    {
        std::unique_lock lock(m_txn_mtx);
        m_transactions[transaction_id] = rec;
    }

    return transaction_id;
}

int mem_store::set_transaction_source(int transaction_id, const std::string& client_id, const std::string& client_type)
{
    {
        std::unique_lock lock(m_txn_mtx);
        auto it = m_transactions.find(transaction_id);
        if (it != m_transactions.end())
        {
            it->second.source_id = client_id;
            it->second.source_type = client_type;
        }
    }

    try
    {
        auto guard = m_pool.acquire();
        return ::set_transaction_source(guard.get(), transaction_id, client_id, client_type);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting transaction source: " << ex.what() << std::endl;
        return -1;
    }
}

int mem_store::mark_transaction_assigned(int transaction_id)
{
    {
        std::unique_lock lock(m_txn_mtx);
        auto it = m_transactions.find(transaction_id);
        if (it != m_transactions.end())
            it->second.status = "ASSIGNED";
    }

    try
    {
        auto guard = m_pool.acquire();
        return ::mark_transaction_assigned(guard.get(), transaction_id);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting transaction assigned: " << ex.what() << std::endl;
        return -1;
    }
}

int mem_store::mark_transaction_dispatched(int transaction_id, const std::string& dispatch_timestamp)
{
    {
        std::unique_lock lock(m_txn_mtx);
        auto it = m_transactions.find(transaction_id);
        if (it != m_transactions.end())
            it->second.status = "DISPATCHED";
    }

    try
    {
        auto guard = m_pool.acquire();
        return ::mark_transaction_dispatched(guard.get(), transaction_id, dispatch_timestamp);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting transaction dispatched: " << ex.what() << std::endl;
        return -1;
    }
}

int mem_store::complete_transaction(int transaction_id, const std::string& reception_timestamp)
{
    {
        std::unique_lock lock(m_txn_mtx);
        m_transactions.erase(transaction_id);
    }

    try
    {
        auto guard = m_pool.acquire();
        return ::complete_transaction(guard.get(), transaction_id, reception_timestamp);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[MEM_STORE] Error persisting transaction complete: " << ex.what() << std::endl;
        return -1;
    }
}

int mem_store::find_transaction(const std::string& source_id, const std::string& destination_id,
                                const std::string& status)
{
    std::shared_lock lock(m_txn_mtx);

    int best_id = -1;
    for (const auto& [id, rec] : m_transactions)
    {
        if (!status.empty() && rec.status != status)
            continue;
        if (!source_id.empty() && rec.source_id != source_id)
            continue;
        if (!destination_id.empty() && rec.destination_id != destination_id)
            continue;
        if (id > best_id)
            best_id = id;
    }

    return best_id;
}

int mem_store::get_transaction(int transaction_id, transaction_record& out)
{
    std::shared_lock lock(m_txn_mtx);
    auto it = m_transactions.find(transaction_id);
    if (it == m_transactions.end())
        return -1;
    out = it->second;
    return 0;
}

int mem_store::get_pending_transactions(transaction_record* out, int max_count)
{
    if (!out || max_count <= 0)
        return 0;

    std::shared_lock lock(m_txn_mtx);

    std::vector<const transaction_record*> pending;
    for (const auto& [id, rec] : m_transactions)
    {
        if (rec.status == "PENDING")
            pending.push_back(&rec);
    }

    std::sort(pending.begin(), pending.end(), [](const transaction_record* a, const transaction_record* b) {
        return a->transaction_id < b->transaction_id;
    });

    int count = std::min(static_cast<int>(pending.size()), max_count);
    for (int i = 0; i < count; i++)
        out[i] = *pending[i];

    return count;
}
