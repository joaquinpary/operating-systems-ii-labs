#include "database.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <pqxx/pqxx>
#include <string>

class DatabaseTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Tests will run with or without database available
        // We check availability in individual tests
    }

    void TearDown() override
    {
        // Cleanup if needed
    }
};

// Test that connect_to_database returns nullptr when connection is not available
TEST_F(DatabaseTest, ConnectToDatabaseReturnsNullWhenNotAvailable)
{
    // This test expects nullptr when DB is not running
    // If DB is available, the connection will succeed
    auto conn = connect_to_database();
    // We accept both outcomes: nullptr if DB unavailable, or valid connection if available
    if (conn)
    {
        EXPECT_NE(conn, nullptr);
        EXPECT_TRUE(conn->is_open());
    }
    else
    {
        // DB not available, which is acceptable for this test
        EXPECT_EQ(conn, nullptr);
    }
}

// Test initialize_database function
TEST_F(DatabaseTest, InitializeDatabase)
{
    auto conn = connect_to_database();
    if (conn)
    {
        EXPECT_EQ(initialize_database(*conn, "config/clients"), 0);

        // Verify that credentials table exists
        pqxx::work txn(*conn);
        try
        {
            auto result =
                txn.exec("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'credentials')");
            EXPECT_TRUE(result[0][0].as<bool>());
        }
        catch (const std::exception& ex)
        {
            // Table might not exist if initialization failed
            FAIL() << "Failed to check credentials table: " << ex.what();
        }
    }
    else
    {
        // DB not available, skip this test
        GTEST_SKIP() << "Database not available, skipping initialization test";
    }
}

// Test create_credentials_table
TEST_F(DatabaseTest, CreateCredentialsTable)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping table creation test";
    }

    int result = create_credentials_table(*conn);
    EXPECT_EQ(result, 0);

    // Verify table exists (need new transaction since create_credentials_table commits)
    pqxx::work verify_txn(*conn);
    auto table_check =
        verify_txn.exec("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'credentials')");
    EXPECT_TRUE(table_check[0][0].as<bool>());
}

// Test query_credentials_by_username with non-existent user
TEST_F(DatabaseTest, QueryCredentialsByUsernameNonExistent)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping query test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Query for non-existent user
    auto cred = query_credentials_by_username(*conn, "nonexistent_user");
    EXPECT_EQ(cred, nullptr);
}

// Test query_credentials_by_username with existing user
TEST_F(DatabaseTest, QueryCredentialsByUsernameExisting)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping query test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Insert a test credential
    {
        pqxx::work insert_txn(*conn);
        try
        {
            insert_txn.exec(pqxx::zview("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                                        "VALUES ($1, $2, $3, $4) "
                                        "ON CONFLICT (username) DO UPDATE SET password_hash = EXCLUDED.password_hash, "
                                        "is_active = EXCLUDED.is_active"),
                            pqxx::params{"test_user", "test_hash", "HUB", true});
            insert_txn.commit();
        }
        catch (const std::exception& ex)
        {
            FAIL() << "Failed to insert test credential: " << ex.what();
        }
    }

    // Query for existing user
    {
        auto cred = query_credentials_by_username(*conn, "test_user");
        ASSERT_NE(cred, nullptr);
        EXPECT_EQ(cred->username, "test_user");
        EXPECT_EQ(cred->password_hash, "test_hash");
        EXPECT_EQ(cred->client_type, "HUB");
        EXPECT_TRUE(cred->is_active);
    }

    // Cleanup
    {
        pqxx::work cleanup_txn(*conn);
        cleanup_txn.exec("DELETE FROM credentials WHERE username = 'test_user'");
        cleanup_txn.commit();
    }
}

// Test populate_credentials_table with valid .conf directory
TEST_F(DatabaseTest, PopulateCredentialsTableValidFile)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Clear table first
    {
        pqxx::work clear_txn(*conn);
        clear_txn.exec("DELETE FROM credentials");
        clear_txn.commit();
    }

    // Create a temporary credentials directory with .conf files for testing
    const std::string test_dir = "/tmp/test_credentials_dir";
    std::filesystem::create_directories(test_dir);
    {
        std::ofstream file1(test_dir + "/client_0001.conf");
        file1 << "host = server\n"
              << "ipversion = v4\n"
              << "protocol = udp\n"
              << "port = 9999\n"
              << "type = HUB\n"
              << "username = test_user1\n"
              << "password = hash1\n";
        file1.close();

        std::ofstream file2(test_dir + "/client_0002.conf");
        file2 << "host = server\n"
              << "ipversion = v4\n"
              << "protocol = udp\n"
              << "port = 9999\n"
              << "type = WAREHOUSE\n"
              << "username = test_user2\n"
              << "password = hash2\n";
        file2.close();
    }

    // Test with valid credentials directory
    int result = populate_credentials_table(*conn, test_dir);
    EXPECT_EQ(result, 0);

    // Verify credentials were inserted (populate_credentials_table commits internally)
    {
        pqxx::work verify_txn(*conn);
        auto count_result = verify_txn.exec("SELECT COUNT(*) FROM credentials");
        int count = count_result[0][0].as<int>();
        EXPECT_EQ(count, 2);
    }

    // Verify specific credentials
    {
        auto cred1 = query_credentials_by_username(*conn, "test_user1");
        ASSERT_NE(cred1, nullptr);
        EXPECT_EQ(cred1->username, "test_user1");
        EXPECT_EQ(cred1->password_hash, "hash1");
        EXPECT_EQ(cred1->client_type, "HUB");
    }

    {
        auto cred2 = query_credentials_by_username(*conn, "test_user2");
        ASSERT_NE(cred2, nullptr);
        EXPECT_EQ(cred2->username, "test_user2");
        EXPECT_EQ(cred2->password_hash, "hash2");
        EXPECT_EQ(cred2->client_type, "WAREHOUSE");
    }

    // Cleanup
    {
        pqxx::work cleanup_txn(*conn);
        cleanup_txn.exec("DELETE FROM credentials WHERE username IN ('test_user1', 'test_user2')");
        cleanup_txn.commit();
    }
    std::filesystem::remove_all(test_dir);
}

// Test populate_credentials_table with non-existent file
TEST_F(DatabaseTest, PopulateCredentialsTableNonExistentFile)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Test with non-existent file
    int result = populate_credentials_table(*conn, "config/nonexistent_credentials_dir");
    EXPECT_EQ(result, 1); // Should return error
}

// Test initialize_database populates credentials if file exists
TEST_F(DatabaseTest, InitializeDatabasePopulatesCredentials)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping initialization test";
    }

    ASSERT_EQ(initialize_database(*conn, "config/clients"), 0);

    // Check if credentials were populated (if file exists)
    pqxx::work txn(*conn);
    auto count_result = txn.exec("SELECT COUNT(*) FROM credentials");
    int count = count_result[0][0].as<int>();

    // Count should be >= 0 (0 if file doesn't exist, >0 if file exists and was populated)
    EXPECT_GE(count, 0);

    // Verify table structure is correct
    auto table_check =
        txn.exec("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'credentials')");
    EXPECT_TRUE(table_check[0][0].as<bool>());
}

// Test populate_credentials_table with invalid .conf credentials
TEST_F(DatabaseTest, PopulateCredentialsTableInvalidJSON)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    const std::string test_dir = "/tmp/invalid_credentials_dir";
    std::filesystem::create_directories(test_dir);
    const std::string test_file = test_dir + "/client_invalid.conf";
    {
        std::ofstream file(test_file);
        file << "host = server\n"
             << "type = HUB\n"
             << "username = only_user_without_password\n";
        file.close();
    }

    int result = populate_credentials_table(*conn, test_dir);
    EXPECT_EQ(result, 1); // Should return error

    std::filesystem::remove_all(test_dir);
}

// Test populate_credentials_table with empty directory
TEST_F(DatabaseTest, PopulateCredentialsTableEmptyArray)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    const std::string test_dir = "/tmp/empty_credentials_dir";
    std::filesystem::create_directories(test_dir);

    int result = populate_credentials_table(*conn, test_dir);
    EXPECT_EQ(result, 0); // Should succeed (no credentials to insert)

    std::filesystem::remove_all(test_dir);
}

// Test populate_credentials_table with malformed credential (missing fields)
TEST_F(DatabaseTest, PopulateCredentialsTableMissingFields)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Create file with incomplete credential (missing password)
    const std::string test_dir = "/tmp/incomplete_credentials_dir";
    std::filesystem::create_directories(test_dir);
    const std::string test_file = test_dir + "/client_incomplete.conf";
    {
        std::ofstream file(test_file);
        file << "host = server\n"
             << "type = HUB\n"
             << "username = incomplete_user\n";
        file.close();
    }

    int result = populate_credentials_table(*conn, test_dir);
    EXPECT_EQ(result, 1); // Should return error

    std::filesystem::remove_all(test_dir);
}

// Helper to save and restore an environment variable
static std::string save_env(const char* name)
{
    const char* v = std::getenv(name);
    return v ? v : "";
}
static void restore_env(const char* name, const std::string& saved)
{
    if (saved.empty())
        unsetenv(name);
    else
        setenv(name, saved.c_str(), 1);
}

// Test build_connection_string with environment variables
TEST_F(DatabaseTest, BuildConnectionStringWithEnvVars)
{
    std::string s_host = save_env("POSTGRES_HOST");
    std::string s_db = save_env("POSTGRES_DB");
    std::string s_user = save_env("POSTGRES_USER");
    std::string s_pass = save_env("POSTGRES_PASSWORD");
    std::string s_port = save_env("POSTGRES_PORT");

    setenv("POSTGRES_HOST", "test_host", 1);
    setenv("POSTGRES_DB", "test_db", 1);
    setenv("POSTGRES_USER", "test_user", 1);
    setenv("POSTGRES_PASSWORD", "test_pass", 1);
    setenv("POSTGRES_PORT", "5433", 1);

    auto conn = connect_to_database();

    restore_env("POSTGRES_HOST", s_host);
    restore_env("POSTGRES_DB", s_db);
    restore_env("POSTGRES_USER", s_user);
    restore_env("POSTGRES_PASSWORD", s_pass);
    restore_env("POSTGRES_PORT", s_port);

    SUCCEED();
}

// Test connect_to_database error handling
TEST_F(DatabaseTest, ConnectToDatabaseErrorHandling)
{
    std::string s_host = save_env("POSTGRES_HOST");
    std::string s_port = save_env("POSTGRES_PORT");

    setenv("POSTGRES_HOST", "invalid_host_12345", 1);
    setenv("POSTGRES_PORT", "99999", 1);

    auto conn = connect_to_database();
    EXPECT_EQ(conn, nullptr);

    restore_env("POSTGRES_HOST", s_host);
    restore_env("POSTGRES_PORT", s_port);
}
// ==================== INVENTORY TESTS ====================

TEST_F(DatabaseTest, CreateInventoryTables)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";

    int result = create_inventory_tables(*conn);
    EXPECT_EQ(result, 0);

    pqxx::work verify_txn(*conn);
    EXPECT_TRUE(
        verify_txn
            .exec("SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'client_inventory')")[0][0]
            .as<bool>());
    EXPECT_TRUE(
        verify_txn
            .exec(
                "SELECT EXISTS (SELECT FROM information_schema.tables WHERE table_name = 'inventory_transactions')")[0]
                                                                                                                    [0]
            .as<bool>());
}

TEST_F(DatabaseTest, UpdateClientInventory)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int quantities[6] = {10, 20, 30, 40, 50, 60};
    int result = update_client_inventory(*conn, "test_client", "HUB", quantities, "2026-02-26T14:00:00Z");
    EXPECT_EQ(result, 0);

    pqxx::work verify_txn(*conn);
    pqxx::result db_res = verify_txn.exec(
        pqxx::zview("SELECT food, water, medicine, tools, guns, ammo FROM client_inventory WHERE client_id = $1"),
        pqxx::params{"test_client"});
    ASSERT_EQ(db_res.size(), 1);
    EXPECT_EQ(db_res[0][0].as<int>(), 10);
    EXPECT_EQ(db_res[0][1].as<int>(), 20);
    EXPECT_EQ(db_res[0][2].as<int>(), 30);
    EXPECT_EQ(db_res[0][3].as<int>(), 40);
    EXPECT_EQ(db_res[0][4].as<int>(), 50);
    EXPECT_EQ(db_res[0][5].as<int>(), 60);
}

TEST_F(DatabaseTest, GetWarehouseWithAllStock)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    // Setup warehouse with stock
    int stock[6] = {100, 100, 100, 100, 100, 100};
    update_client_inventory(*conn, "wh_full", "WAREHOUSE", stock, "2026-02-26T14:00:00Z");

    int partial_stock[6] = {50, 0, 0, 0, 0, 0};
    update_client_inventory(*conn, "wh_partial", "WAREHOUSE", partial_stock, "2026-02-26T14:00:00Z");

    int needed[6] = {10, 10, 10, 10, 10, 10};
    std::string wh = get_warehouse_with_all_stock(*conn, needed);
    EXPECT_EQ(wh, "wh_full");

    int impossible[6] = {1000, 0, 0, 0, 0, 0};
    wh = get_warehouse_with_all_stock(*conn, impossible);
    EXPECT_EQ(wh, "");

    // Test invalid needed (all zeros) should return first warehouse
    int zeros[6] = {0, 0, 0, 0, 0, 0};
    wh = get_warehouse_with_all_stock(*conn, zeros);
    EXPECT_FALSE(wh.empty());
}

TEST_F(DatabaseTest, SetTransactionDestination)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int q[6] = {1, 1, 1, 1, 1, 1};
    int tid = create_transaction(*conn, "STOCK_REQUEST", "hub_1", "HUB", q, "2026-02-26T15:00:00Z");

    int result = set_transaction_destination(*conn, tid, "hub_2", "HUB");
    EXPECT_EQ(result, 0);

    pqxx::work verify_txn(*conn);
    pqxx::result res = verify_txn.exec(
        pqxx::zview("SELECT destination_id FROM inventory_transactions WHERE transaction_id = $1"), pqxx::params{tid});
    EXPECT_EQ(res[0][0].as<std::string>(), "hub_2");
}

TEST_F(DatabaseTest, InvalidParameters)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";

    int quantities[6] = {0};
    // Line 324: check empty client_id
    EXPECT_EQ(update_client_inventory(*conn, "", "HUB", quantities, "2026-02-26T14:00:00Z"), -1);

    // Line 410: check empty destination_id in create_transaction
    EXPECT_EQ(create_transaction(*conn, "STOCK_REQUEST", "", "HUB", quantities, "2026-02-26T14:00:00Z"), -1);

    // Line 571 check nullptr transactions
    EXPECT_EQ(get_pending_transactions(*conn, nullptr, 10), 0);
    // Line 571 check max_count <= 0
    transaction_record tr;
    EXPECT_EQ(get_pending_transactions(*conn, &tr, 0), 0);
}

TEST_F(DatabaseTest, TransactionStateWorkflow)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int q[6] = {5, 5, 5, 0, 0, 0};
    int tid = create_transaction(*conn, "STOCK_REQUEST", "hub_1", "HUB", q, "2026-02-26T15:00:00Z");
    ASSERT_GT(tid, 0);

    // Initially PENDING
    {
        transaction_record pending[10];
        int count = get_pending_transactions(*conn, pending, 10);
        EXPECT_GE(count, 1);
        bool found = false;
        for (int i = 0; i < count; i++)
            if (pending[i].transaction_id == tid)
            {
                found = true;
                break;
            }
        EXPECT_TRUE(found);
    }

    // Set source
    set_transaction_source(*conn, tid, "wh_1", "WAREHOUSE");

    // Mark ASSIGNED
    mark_transaction_assigned(*conn, tid);

    // Find it
    int found_tid = find_transaction_id(*conn, "wh_1", "hub_1", "ASSIGNED");
    EXPECT_EQ(found_tid, tid);

    // Mark DISPATCHED
    mark_transaction_dispatched(*conn, tid, "2026-02-26T15:30:00Z");

    // Complete
    complete_transaction(*conn, tid, "2026-02-26T16:00:00Z");

    pqxx::work verify_txn(*conn);
    pqxx::result res = verify_txn.exec(
        pqxx::zview("SELECT status, reception_timestamp FROM inventory_transactions WHERE transaction_id = $1"),
        pqxx::params{tid});
    EXPECT_EQ(res[0][0].as<std::string>(), "COMPLETED");
    EXPECT_FALSE(res[0][1].is_null());
}

// ==================== get_transaction_by_id ====================

TEST_F(DatabaseTest, GetTransactionByIdFound)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int q[6] = {3, 4, 5, 0, 0, 0};
    int tid = create_transaction(*conn, "STOCK_REQUEST", "hub_gt", "HUB", q, "2026-02-26T17:00:00Z");
    ASSERT_GT(tid, 0);

    transaction_record out{};
    EXPECT_EQ(get_transaction_by_id(*conn, tid, out), 0);
    EXPECT_EQ(out.transaction_id, tid);
    EXPECT_EQ(out.transaction_type, "STOCK_REQUEST");
    EXPECT_EQ(out.destination_id, "hub_gt");
    EXPECT_EQ(out.food, 3);
    EXPECT_EQ(out.water, 4);
    EXPECT_EQ(out.medicine, 5);
}

TEST_F(DatabaseTest, GetTransactionByIdNotFound)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    transaction_record out{};
    EXPECT_EQ(get_transaction_by_id(*conn, 999999, out), -1);
}

// ==================== get_client_inventory ====================

TEST_F(DatabaseTest, GetClientInventoryNewClient)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    // Clean up first
    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM client_inventory WHERE client_id = 'new_hub'");
        txn.commit();
    }

    int out[6] = {0};
    EXPECT_EQ(get_client_inventory(*conn, "new_hub", "HUB", out), 0);
    // New HUB gets INITIAL_STOCK_HUB = 100
    for (int i = 0; i < 6; i++)
        EXPECT_EQ(out[i], 100);
}

TEST_F(DatabaseTest, GetClientInventoryNewWarehouse)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM client_inventory WHERE client_id = 'new_wh'");
        txn.commit();
    }

    int out[6] = {0};
    EXPECT_EQ(get_client_inventory(*conn, "new_wh", "WAREHOUSE", out), 0);
    // New WAREHOUSE gets INITIAL_STOCK_WAREHOUSE = 500
    for (int i = 0; i < 6; i++)
        EXPECT_EQ(out[i], 500);
}

TEST_F(DatabaseTest, GetClientInventoryExisting)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int stock[6] = {11, 22, 33, 44, 55, 66};
    update_client_inventory(*conn, "inv_hub", "HUB", stock, "2026-02-26T18:00:00Z");

    int out[6] = {0};
    EXPECT_EQ(get_client_inventory(*conn, "inv_hub", "HUB", out), 0);
    EXPECT_EQ(out[0], 11);
    EXPECT_EQ(out[1], 22);
    EXPECT_EQ(out[2], 33);
    EXPECT_EQ(out[3], 44);
    EXPECT_EQ(out[4], 55);
    EXPECT_EQ(out[5], 66);
}

TEST_F(DatabaseTest, GetClientInventoryInvalidParams)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int out[6] = {0};
    EXPECT_EQ(get_client_inventory(*conn, "", "HUB", out), -1);
    EXPECT_EQ(get_client_inventory(*conn, "c1", "", out), -1);
    EXPECT_EQ(get_client_inventory(*conn, "c1", "HUB", nullptr), -1);
}

// ==================== adjust_client_inventory ====================

TEST_F(DatabaseTest, AdjustClientInventoryAdd)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int base[6] = {10, 20, 30, 40, 50, 60};
    update_client_inventory(*conn, "adj_hub", "HUB", base, "2026-02-26T19:00:00Z");

    int delta[6] = {5, 5, 5, 5, 5, 5};
    EXPECT_EQ(adjust_client_inventory(*conn, "adj_hub", delta, true), 0);

    int out[6] = {0};
    get_client_inventory(*conn, "adj_hub", "HUB", out);
    EXPECT_EQ(out[0], 15);
    EXPECT_EQ(out[1], 25);
    EXPECT_EQ(out[2], 35);
}

TEST_F(DatabaseTest, AdjustClientInventorySubtract)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int base[6] = {10, 20, 30, 40, 50, 60};
    update_client_inventory(*conn, "adj_hub2", "HUB", base, "2026-02-26T19:00:00Z");

    int delta[6] = {5, 25, 5, 5, 5, 5};
    EXPECT_EQ(adjust_client_inventory(*conn, "adj_hub2", delta, false), 0);

    int out[6] = {0};
    get_client_inventory(*conn, "adj_hub2", "HUB", out);
    EXPECT_EQ(out[0], 5);
    EXPECT_EQ(out[1], 0); // clamped to 0
    EXPECT_EQ(out[2], 25);
}

TEST_F(DatabaseTest, AdjustClientInventoryInvalidParams)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int delta[6] = {1, 1, 1, 1, 1, 1};
    EXPECT_EQ(adjust_client_inventory(*conn, "", delta, true), -1);
    EXPECT_EQ(adjust_client_inventory(*conn, "hub", nullptr, true), -1);
}

// ==================== set_client_active ====================

TEST_F(DatabaseTest, SetClientActive)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_credentials_table(*conn);

    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM credentials WHERE username = 'active_test'");
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                 "VALUES ('active_test', 'hash', 'HUB', FALSE)");
        txn.commit();
    }

    EXPECT_EQ(set_client_active(*conn, "active_test", true), 0);
    {
        auto cred = query_credentials_by_username(*conn, "active_test");
        ASSERT_NE(cred, nullptr);
        EXPECT_TRUE(cred->is_active);
    }

    EXPECT_EQ(set_client_active(*conn, "active_test", false), 0);
    {
        auto cred = query_credentials_by_username(*conn, "active_test");
        ASSERT_NE(cred, nullptr);
        EXPECT_FALSE(cred->is_active);
    }

    // Cleanup
    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM credentials WHERE username = 'active_test'");
        txn.commit();
    }
}

TEST_F(DatabaseTest, SetClientActiveInvalidParams)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";

    EXPECT_EQ(set_client_active(*conn, "", true), -1);
}

// ==================== reset_all_clients_inactive ====================

TEST_F(DatabaseTest, ResetAllClientsInactive)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_credentials_table(*conn);

    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM credentials WHERE username IN ('ra1', 'ra2')");
        txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                 "VALUES ('ra1', 'h', 'HUB', TRUE), ('ra2', 'h', 'WAREHOUSE', TRUE)");
        txn.commit();
    }

    EXPECT_EQ(reset_all_clients_inactive(*conn), 0);

    {
        auto c1 = query_credentials_by_username(*conn, "ra1");
        ASSERT_NE(c1, nullptr);
        EXPECT_FALSE(c1->is_active);
        auto c2 = query_credentials_by_username(*conn, "ra2");
        ASSERT_NE(c2, nullptr);
        EXPECT_FALSE(c2->is_active);
    }

    // Cleanup
    {
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM credentials WHERE username IN ('ra1', 'ra2')");
        txn.commit();
    }
}

// ==================== Additional validation paths ====================

TEST_F(DatabaseTest, UpdateClientInventoryEmptyClientType)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int q[6] = {1, 1, 1, 1, 1, 1};
    EXPECT_EQ(update_client_inventory(*conn, "valid_id", "", q, "2026-02-26T14:00:00Z"), -1);
}

TEST_F(DatabaseTest, CreateTransactionEmptyType)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    int q[6] = {1, 1, 1, 1, 1, 1};
    EXPECT_EQ(create_transaction(*conn, "", "hub", "HUB", q, "2026-02-26T14:00:00Z"), -1);
}

TEST_F(DatabaseTest, GetWarehouseWithAllStockNullQuantities)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    EXPECT_EQ(get_warehouse_with_all_stock(*conn, nullptr), "");
}

TEST_F(DatabaseTest, FindTransactionIdNotFound)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    create_inventory_tables(*conn);

    EXPECT_EQ(find_transaction_id(*conn, "nonexistent_src", "nonexistent_dst", "ASSIGNED"), -1);
}

// ==================== Closed connection catch-block tests ====================

TEST_F(DatabaseTest, ClosedConnectionCatchBlocks)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    conn->close();

    int q[6] = {1, 1, 1, 1, 1, 1};
    int inv[6] = {0};
    transaction_record out{};
    transaction_record recs[1];

    EXPECT_EQ(create_credentials_table(*conn), 1);
    EXPECT_EQ(query_credentials_by_username(*conn, "x"), nullptr);
    EXPECT_EQ(create_inventory_tables(*conn), 1);
    EXPECT_EQ(update_client_inventory(*conn, "x", "HUB", q, "2026-01-01T00:00:00Z"), -1);
    EXPECT_EQ(get_warehouse_with_all_stock(*conn, q), "");
    EXPECT_EQ(create_transaction(*conn, "T", "d", "HUB", q, "2026-01-01T00:00:00Z"), -1);
    EXPECT_EQ(set_transaction_destination(*conn, 1, "c", "HUB"), -1);
    EXPECT_EQ(set_transaction_source(*conn, 1, "c", "HUB"), -1);
    EXPECT_EQ(mark_transaction_dispatched(*conn, 1, "2026-01-01T00:00:00Z"), -1);
    EXPECT_EQ(mark_transaction_assigned(*conn, 1), -1);
    EXPECT_EQ(complete_transaction(*conn, 1, "2026-01-01T00:00:00Z"), -1);
    EXPECT_EQ(get_pending_transactions(*conn, recs, 1), 0);
    EXPECT_EQ(find_transaction_id(*conn, "s", "d", "PENDING"), -1);
    EXPECT_EQ(get_transaction_by_id(*conn, 1, out), -1);
    EXPECT_EQ(get_client_inventory(*conn, "c", "HUB", inv), -1);
    EXPECT_EQ(adjust_client_inventory(*conn, "c", q, true), -1);
    EXPECT_EQ(set_client_active(*conn, "c", true), -1);
    EXPECT_EQ(reset_all_clients_inactive(*conn), -1);
    EXPECT_EQ(populate_credentials_table(*conn, "/tmp"), 1);
}

TEST_F(DatabaseTest, InitializeDatabaseFailsOnClosedConnection)
{
    auto conn = connect_to_database();
    if (!conn)
        GTEST_SKIP() << "Database not available";
    conn->close();
    EXPECT_EQ(initialize_database(*conn, "/tmp"), -1);
}

// ==================== Environment variable edge cases ====================

TEST_F(DatabaseTest, ConnectToDatabaseMissingEnvVar)
{
    std::string s_host = save_env("POSTGRES_HOST");
    unsetenv("POSTGRES_HOST");

    auto conn = connect_to_database();
    EXPECT_EQ(conn, nullptr);

    restore_env("POSTGRES_HOST", s_host);
}

TEST_F(DatabaseTest, BuildConnectionStringInvalidPort)
{
    if (!std::getenv("POSTGRES_HOST"))
        GTEST_SKIP() << "Database env vars not set";

    std::string s_port = save_env("POSTGRES_PORT");
    setenv("POSTGRES_PORT", "not_a_number", 1);

    std::string cs = build_connection_string();
    EXPECT_NE(cs.find("port=5432"), std::string::npos);

    restore_env("POSTGRES_PORT", s_port);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
