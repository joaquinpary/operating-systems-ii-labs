#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <pqxx/pqxx>
#include <server/connection_pool.hpp>
#include <server/database.hpp>
#include <server/inventory_manager.hpp>

class InventoryManagerTest : public ::testing::Test
{
  protected:
    std::unique_ptr<pqxx::connection> db_conn;
    std::shared_ptr<connection_pool> db_pool;
    std::unique_ptr<inventory_manager> inv_mgr;

    void SetUp() override
    {
        db_conn = connect_to_database();
        if (!db_conn)
        {
            GTEST_SKIP() << "Database not available, skipping inventory_manager tests";
        }

        // Drop and recreate tables to ensure schema changes are applied
        {
            pqxx::work txn(*db_conn);
            txn.exec("DROP TABLE IF EXISTS inventory_transactions");
            txn.exec("DROP TABLE IF EXISTS client_inventory");
            txn.commit();
        }
        create_inventory_tables(*db_conn);

        // Clean up and setup initial data
        pqxx::work txn(*db_conn);
        txn.exec("DELETE FROM credentials");

        // Add a hub and a warehouse
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) VALUES ('hub_1', 'hash', "
                 "'HUB', TRUE)");
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) VALUES ('warehouse_1', "
                 "'hash', 'WAREHOUSE', TRUE)");

        // Give the warehouse some stock (Items 1-6) using the wide schema
        txn.exec("INSERT INTO client_inventory (client_id, client_type, food, water, medicine, tools, guns, ammo, "
                 "last_updated) "
                 "VALUES ('warehouse_1', 'WAREHOUSE', 100, 100, 100, 100, 100, 100, NOW())");

        txn.commit();

        db_pool = std::make_shared<connection_pool>(build_connection_string(), 1);
        inv_mgr = std::make_unique<inventory_manager>(*db_pool);
    }

    void TearDown() override
    {
        inv_mgr.reset();
        db_pool.reset();
        db_conn.reset();
    }
};

TEST_F(InventoryManagerTest, HandleStockRequestDirectFullfillment)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strcpy(msg.source_id, "hub_1");
    strcpy(msg.source_role, "HUB");
    strcpy(msg.timestamp, "2026-02-26T10:00:00Z");

    // Request 10 units of item 1 (Food)
    msg.payload.stock_request.items[0].item_id = 1;
    msg.payload.stock_request.items[0].quantity = 10;

    stock_request_result result = inv_mgr->handle_stock_request(msg);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.warehouse_assigned);
    EXPECT_EQ(result.assigned_warehouse_id, "warehouse_1");
    EXPECT_GT(result.transaction_id, 0);

    // Check DB status
    pqxx::work txn(*db_conn);
    pqxx::result db_res = txn.exec(pqxx::zview("SELECT status FROM inventory_transactions WHERE transaction_id = $1"),
                                   pqxx::params{result.transaction_id});
    EXPECT_EQ(db_res[0][0].as<std::string>(), "ASSIGNED");
}

TEST_F(InventoryManagerTest, HandleStockRequestPendingState)
{
    // Clear warehouse stock so request goes to PENDING
    {
        pqxx::work txn(*db_conn);
        txn.exec("UPDATE client_inventory SET food = 0, water = 0, medicine = 0, tools = 0, guns = 0, ammo = 0 WHERE "
                 "client_id = 'warehouse_1'");
        txn.commit();
    }

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strcpy(msg.source_id, "hub_1");
    strcpy(msg.source_role, "HUB");
    strcpy(msg.timestamp, "2026-02-26T10:05:00Z");

    msg.payload.stock_request.items[0].item_id = 1;
    msg.payload.stock_request.items[0].quantity = 50;

    stock_request_result result = inv_mgr->handle_stock_request(msg);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.warehouse_assigned);
    EXPECT_GT(result.transaction_id, 0);

    // Check DB status
    pqxx::work txn(*db_conn);
    pqxx::result db_res = txn.exec(pqxx::zview("SELECT status FROM inventory_transactions WHERE transaction_id = $1"),
                                   pqxx::params{result.transaction_id});
    EXPECT_EQ(db_res[0][0].as<std::string>(), "PENDING");
}

TEST_F(InventoryManagerTest, HandleReplenishRequest)
{
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strcpy(msg.source_id, "warehouse_1");
    strcpy(msg.source_role, "WAREHOUSE");
    strcpy(msg.timestamp, "2026-02-26T10:10:00Z");

    msg.payload.restock_notice.items[0].item_id = 1;
    msg.payload.restock_notice.items[0].quantity = 500;

    stock_request_result result = inv_mgr->handle_replenish_request(msg);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.warehouse_assigned);
    EXPECT_EQ(result.assigned_warehouse_id, "warehouse_1");
    EXPECT_GT(result.transaction_id, 0);

    // Replenish requests are authorized immediately by the server.
    pqxx::work txn(*db_conn);
    pqxx::result db_res = txn.exec(pqxx::zview("SELECT status FROM inventory_transactions WHERE transaction_id = $1"),
                                   pqxx::params{result.transaction_id});
    EXPECT_EQ(db_res[0][0].as<std::string>(), "DISPATCHED");
}

TEST_F(InventoryManagerTest, HandleShipmentNotice)
{
    // First create an ASSIGNED transaction
    int tid;
    {
        pqxx::work txn(*db_conn);
        tid = txn.exec(pqxx::zview(
                           "INSERT INTO inventory_transactions (transaction_type, source_id, source_type, "
                           "destination_id, destination_type, status, food) VALUES ('STOCK_REQUEST', 'warehouse_1', "
                           "'WAREHOUSE', 'hub_1', 'HUB', 'ASSIGNED', 10) RETURNING transaction_id"))
                  .one_row()[0]
                  .as<int>();
        txn.commit();
    }

    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strcpy(msg.source_id, "warehouse_1");
    strcpy(msg.source_role, "WAREHOUSE");
    strcpy(msg.timestamp, "2026-02-26T12:00:00Z");

    stock_request_result ret = inv_mgr->handle_shipment_notice(msg);
    EXPECT_TRUE(ret.success);

    // Verify status changed to DISPATCHED
    pqxx::work txn(*db_conn);
    pqxx::result db_res =
        txn.exec(pqxx::zview("SELECT status, dispatch_timestamp FROM inventory_transactions WHERE transaction_id = $1"),
                 pqxx::params{tid});
    EXPECT_EQ(db_res[0][0].as<std::string>(), "DISPATCHED");
    // Database format might be YYYY-MM-DD HH:MM:SS instead of ISO-8601
    std::string db_ts = db_res[0][1].as<std::string>();
    EXPECT_TRUE(db_ts == "2026-02-26 12:00:00" || db_ts == "2026-02-26T12:00:00Z");
}

TEST_F(InventoryManagerTest, ProcessPendingOrders)
{
    // 1. Create a PENDING order
    {
        pqxx::work txn(*db_conn);
        txn.exec("INSERT INTO inventory_transactions (transaction_type, destination_id, destination_type, status, "
                 "food) VALUES ('STOCK_REQUEST', 'hub_1', 'HUB', 'PENDING', 50)");
        // Empty warehouse stock
        txn.exec("UPDATE client_inventory SET food = 0, water = 0, medicine = 0, tools = 0, guns = 0, ammo = 0 WHERE "
                 "client_id = 'warehouse_1'");
        txn.commit();
    }

    // 2. Run process_pending_orders - should fulfill nothing
    auto results = inv_mgr->process_pending_orders("warehouse_1");
    EXPECT_TRUE(results.empty());

    // 3. Add stock to warehouse
    {
        pqxx::work txn(*db_conn);
        txn.exec("UPDATE client_inventory SET food = 100, water = 100, medicine = 100, tools = 100, guns = 100, ammo = "
                 "100 WHERE client_id = 'warehouse_1'");
        txn.commit();
    }

    // 4. Run process_pending_orders - should now fulfill the order
    results = inv_mgr->process_pending_orders("warehouse_1");
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].assigned_warehouse_id, "warehouse_1");
    EXPECT_EQ(results[0].requesting_hub_id, "hub_1");

    // 5. Verify DB status is now ASSIGNED
    pqxx::work txn(*db_conn);
    pqxx::result db_res =
        txn.exec(pqxx::zview("SELECT status, source_id FROM inventory_transactions WHERE transaction_id = $1"),
                 pqxx::params{results[0].transaction_id});
    EXPECT_EQ(db_res[0][0].as<std::string>(), "ASSIGNED");
    EXPECT_EQ(db_res[0][1].as<std::string>(), "warehouse_1");
}

TEST_F(InventoryManagerTest, HandleInventoryUpdateTriggersProcessPending)
{
    // Create PENDING order
    {
        pqxx::work txn(*db_conn);
        txn.exec("INSERT INTO inventory_transactions (transaction_type, destination_id, destination_type, status, "
                 "food) VALUES ('STOCK_REQUEST', 'hub_1', 'HUB', 'PENDING', 30)");
        txn.commit();
    }

    // Send inventory update from warehouse with enough stock
    message_t msg;
    memset(&msg, 0, sizeof(message_t));
    strcpy(msg.source_id, "warehouse_1");
    strcpy(msg.source_role, "WAREHOUSE");
    strcpy(msg.timestamp, "2026-02-26T10:15:00Z");

    // Set quantities in payload
    for (int i = 0; i < 6; i++)
        msg.payload.inventory_update.items[i].item_id = i + 1;
    msg.payload.inventory_update.items[0].quantity = 100; // Food

    auto results = inv_mgr->handle_inventory_update(msg);

    // Should have fulfilled the pending order
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].requesting_hub_id, "hub_1");
}
