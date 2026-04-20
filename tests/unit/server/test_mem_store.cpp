#include "mem_store.hpp"

#include "connection_pool.hpp"
#include "database.hpp"

#include <gtest/gtest.h>
#include <pqxx/pqxx>

class MemStoreTest : public ::testing::Test
{
  protected:
    std::unique_ptr<pqxx::connection> db_conn;
    std::shared_ptr<connection_pool> pool;
    std::unique_ptr<mem_store> store;

    void SetUp() override
    {
        db_conn = connect_to_database();
        if (!db_conn)
        {
            GTEST_SKIP() << "Database not available";
        }

        create_credentials_table(*db_conn);
        create_inventory_tables(*db_conn);

        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM credentials");
            txn.exec("DELETE FROM client_inventory");
            txn.exec("DELETE FROM inventory_transactions");
            txn.commit();
        }

        {
            pqxx::work txn(*db_conn);
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('hub1', 'hash1', 'HUB', FALSE)");
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('wh1', 'hash2', 'WAREHOUSE', TRUE)");
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('wh2', 'hash3', 'WAREHOUSE', FALSE)");
            txn.commit();
        }

        pool = std::make_shared<connection_pool>(build_connection_string(), 2);
        store = std::make_unique<mem_store>(*pool);
    }

    void TearDown() override
    {
        store.reset();
        pool.reset();
        if (db_conn)
        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM inventory_transactions");
            txn.exec("DELETE FROM client_inventory");
            txn.exec("DELETE FROM credentials");
            txn.commit();
        }
        db_conn.reset();
    }
};

// ======================= CREDENTIALS =======================

TEST_F(MemStoreTest, LoadCredentialsPopulatesCache)
{
    store->load_credentials();

    auto cred = store->get_credential("hub1");
    ASSERT_NE(cred, nullptr);
    EXPECT_EQ(cred->username, "hub1");
    EXPECT_EQ(cred->client_type, "HUB");
}

TEST_F(MemStoreTest, GetCredentialReturnsNullForUnknownUser)
{
    store->load_credentials();

    auto cred = store->get_credential("nonexistent");
    EXPECT_EQ(cred, nullptr);
}

TEST_F(MemStoreTest, LoadCredentialsTracksActiveClients)
{
    store->load_credentials();

    auto wh1 = store->get_credential("wh1");
    ASSERT_NE(wh1, nullptr);
    EXPECT_TRUE(wh1->is_active);

    auto hub1 = store->get_credential("hub1");
    ASSERT_NE(hub1, nullptr);
    EXPECT_FALSE(hub1->is_active);
}

TEST_F(MemStoreTest, SetActiveTogglesFlag)
{
    store->load_credentials();

    store->set_active("hub1", true);
    auto cred = store->get_credential("hub1");
    ASSERT_NE(cred, nullptr);
    EXPECT_TRUE(cred->is_active);

    store->set_active("hub1", false);
    cred = store->get_credential("hub1");
    ASSERT_NE(cred, nullptr);
    EXPECT_FALSE(cred->is_active);
}

TEST_F(MemStoreTest, ResetAllInactiveClearsActive)
{
    store->load_credentials();
    store->set_active("hub1", true);
    store->set_active("wh2", true);

    store->reset_all_inactive();

    auto hub1 = store->get_credential("hub1");
    ASSERT_NE(hub1, nullptr);
    EXPECT_FALSE(hub1->is_active);

    auto wh1 = store->get_credential("wh1");
    ASSERT_NE(wh1, nullptr);
    EXPECT_FALSE(wh1->is_active);
}

// ======================= INVENTORY =======================

TEST_F(MemStoreTest, UpdateAndGetInventory)
{
    int quantities[6] = {10, 20, 30, 40, 50, 60};
    store->update_inventory("hub1", "HUB", quantities, "2025-01-01T00:00:00");

    int out[6] = {};
    ASSERT_TRUE(store->get_inventory("hub1", "HUB", out));

    for (int i = 0; i < 6; i++)
    {
        EXPECT_EQ(out[i], quantities[i]);
    }
}

TEST_F(MemStoreTest, GetInventoryFromDBWhenNotCached)
{
    {
        auto guard = pool->acquire();
        int quantities[6] = {5, 10, 15, 20, 25, 30};
        ::update_client_inventory(guard.get(), "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    }

    int out[6] = {};
    ASSERT_TRUE(store->get_inventory("hub1", "HUB", out));
    EXPECT_EQ(out[0], 5);
    EXPECT_EQ(out[1], 10);
}

TEST_F(MemStoreTest, AdjustInventoryAdd)
{
    int initial[6] = {100, 200, 300, 400, 500, 600};
    store->update_inventory("wh1", "WAREHOUSE", initial, "2025-01-01T00:00:00");

    int delta[6] = {10, 20, 30, 40, 50, 60};
    store->adjust_inventory("wh1", delta, true);

    int out[6] = {};
    ASSERT_TRUE(store->get_inventory("wh1", "WAREHOUSE", out));
    EXPECT_EQ(out[0], 110);
    EXPECT_EQ(out[3], 440);
}

TEST_F(MemStoreTest, AdjustInventorySubtractClampedToZero)
{
    int initial[6] = {10, 20, 30, 40, 50, 60};
    store->update_inventory("wh1", "WAREHOUSE", initial, "2025-01-01T00:00:00");

    int delta[6] = {100, 100, 100, 100, 100, 100};
    store->adjust_inventory("wh1", delta, false);

    int out[6] = {};
    ASSERT_TRUE(store->get_inventory("wh1", "WAREHOUSE", out));
    for (int i = 0; i < 6; i++)
    {
        EXPECT_GE(out[i], 0);
    }
}

TEST_F(MemStoreTest, FindWarehouseWithStockReturnsMatch)
{
    int stock[6] = {100, 200, 300, 400, 500, 600};
    store->update_inventory("wh1", "WAREHOUSE", stock, "2025-01-01T00:00:00");

    int needed[6] = {50, 50, 50, 50, 50, 50};
    std::string result = store->find_warehouse_with_stock(needed);
    EXPECT_EQ(result, "wh1");
}

TEST_F(MemStoreTest, FindWarehouseWithStockReturnsEmptyWhenInsufficient)
{
    int stock[6] = {10, 10, 10, 10, 10, 10};
    store->update_inventory("wh1", "WAREHOUSE", stock, "2025-01-01T00:00:00");

    int needed[6] = {999, 999, 999, 999, 999, 999};
    std::string result = store->find_warehouse_with_stock(needed);
    EXPECT_TRUE(result.empty());
}

TEST_F(MemStoreTest, FindWarehouseIgnoresNonWarehouses)
{
    int stock[6] = {100, 200, 300, 400, 500, 600};
    store->update_inventory("hub1", "HUB", stock, "2025-01-01T00:00:00");

    int needed[6] = {1, 1, 1, 1, 1, 1};
    std::string result = store->find_warehouse_with_stock(needed);
    EXPECT_TRUE(result.empty());
}

// ======================= TRANSACTIONS =======================

TEST_F(MemStoreTest, CreateTransactionReturnsValidId)
{
    int quantities[6] = {10, 20, 30, 40, 50, 60};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    EXPECT_GT(txn_id, 0);
}

TEST_F(MemStoreTest, GetTransactionRetrievesCreated)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    transaction_record rec{};
    ASSERT_EQ(store->get_transaction(txn_id, rec), 0);
    EXPECT_EQ(rec.transaction_id, txn_id);
    EXPECT_EQ(rec.destination_id, "hub1");
    EXPECT_EQ(rec.status, "PENDING");
    EXPECT_EQ(rec.food, 1);
    EXPECT_EQ(rec.ammo, 6);
}

TEST_F(MemStoreTest, GetTransactionReturnsMinusOneForUnknown)
{
    transaction_record rec{};
    EXPECT_EQ(store->get_transaction(99999, rec), -1);
}

TEST_F(MemStoreTest, SetTransactionSource)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    EXPECT_EQ(store->set_transaction_source(txn_id, "wh1", "WAREHOUSE"), 0);

    transaction_record rec{};
    ASSERT_EQ(store->get_transaction(txn_id, rec), 0);
    EXPECT_EQ(rec.source_id, "wh1");
    EXPECT_EQ(rec.source_type, "WAREHOUSE");
}

TEST_F(MemStoreTest, MarkTransactionAssigned)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    EXPECT_EQ(store->mark_transaction_assigned(txn_id), 0);

    transaction_record rec{};
    ASSERT_EQ(store->get_transaction(txn_id, rec), 0);
    EXPECT_EQ(rec.status, "ASSIGNED");
}

TEST_F(MemStoreTest, MarkTransactionDispatched)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    EXPECT_EQ(store->mark_transaction_dispatched(txn_id, "2025-01-02T00:00:00"), 0);

    transaction_record rec{};
    ASSERT_EQ(store->get_transaction(txn_id, rec), 0);
    EXPECT_EQ(rec.status, "DISPATCHED");
}

TEST_F(MemStoreTest, CompleteTransactionRemovesFromCache)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    EXPECT_EQ(store->complete_transaction(txn_id, "2025-01-03T00:00:00"), 0);

    transaction_record rec{};
    EXPECT_EQ(store->get_transaction(txn_id, rec), -1);
}

TEST_F(MemStoreTest, FindTransactionByDestinationAndStatus)
{
    int quantities[6] = {1, 2, 3, 4, 5, 6};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    int found = store->find_transaction("", "hub1", "PENDING");
    EXPECT_EQ(found, txn_id);
}

TEST_F(MemStoreTest, FindTransactionReturnsMinusOneWhenNoMatch)
{
    int found = store->find_transaction("", "nonexistent", "PENDING");
    EXPECT_EQ(found, -1);
}

TEST_F(MemStoreTest, GetPendingTransactions)
{
    int q1[6] = {1, 2, 3, 4, 5, 6};
    int q2[6] = {10, 20, 30, 40, 50, 60};
    int txn1 = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", q1, "2025-01-01T00:00:00");
    int txn2 = store->create_transaction("REPLENISH", "wh1", "WAREHOUSE", q2, "2025-01-01T01:00:00");
    ASSERT_GT(txn1, 0);
    ASSERT_GT(txn2, 0);

    store->mark_transaction_assigned(txn2);

    transaction_record pending[10];
    int count = store->get_pending_transactions(pending, 10);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(pending[0].transaction_id, txn1);
}

TEST_F(MemStoreTest, GetPendingTransactionsNullBufferReturnsZero)
{
    EXPECT_EQ(store->get_pending_transactions(nullptr, 10), 0);
}

TEST_F(MemStoreTest, GetPendingTransactionsZeroMaxReturnsZero)
{
    transaction_record buf[1];
    EXPECT_EQ(store->get_pending_transactions(buf, 0), 0);
}

TEST_F(MemStoreTest, TransactionLifecycle)
{
    store->load_credentials();

    int stock[6] = {500, 500, 500, 500, 500, 500};
    store->update_inventory("wh1", "WAREHOUSE", stock, "2025-01-01T00:00:00");

    int quantities[6] = {10, 20, 30, 40, 50, 60};
    int txn_id = store->create_transaction("STOCK_REQUEST", "hub1", "HUB", quantities, "2025-01-01T00:00:00");
    ASSERT_GT(txn_id, 0);

    EXPECT_EQ(store->set_transaction_source(txn_id, "wh1", "WAREHOUSE"), 0);
    EXPECT_EQ(store->mark_transaction_assigned(txn_id), 0);
    EXPECT_EQ(store->mark_transaction_dispatched(txn_id, "2025-01-02T00:00:00"), 0);
    EXPECT_EQ(store->complete_transaction(txn_id, "2025-01-03T00:00:00"), 0);

    transaction_record rec{};
    EXPECT_EQ(store->get_transaction(txn_id, rec), -1);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
