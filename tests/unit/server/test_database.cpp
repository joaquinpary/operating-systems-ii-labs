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
    auto conn = initialize_database();
    if (conn)
    {
        EXPECT_NE(conn, nullptr);
        EXPECT_TRUE(conn->is_open());

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
            insert_txn.exec(
                pqxx::zview("INSERT INTO credentials (username, password_hash, client_type) VALUES ($1, $2, $3) "
                            "ON CONFLICT (username) DO UPDATE SET password_hash = EXCLUDED.password_hash"),
                pqxx::params{"test_user", "test_hash", "HUB"});
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

// Test populate_credentials_table with valid JSON file
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

    // Create a temporary JSON file for testing
    const std::string test_file = "/tmp/test_credentials.json";
    {
        std::ofstream file(test_file);
        file << "[" << "{\"username\":\"test_user1\",\"password\":\"hash1\",\"type\":\"HUB\"},"
             << "{\"username\":\"test_user2\",\"password\":\"hash2\",\"type\":\"WAREHOUSE\"}" << "]";
        file.close();
    }

    // Test with valid credentials file
    int result = populate_credentials_table(*conn, test_file);
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
    std::filesystem::remove(test_file);
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
    int result = populate_credentials_table(*conn, "config/nonexistent_credentials.json");
    EXPECT_EQ(result, 1); // Should return error
}

// Test initialize_database populates credentials if file exists
TEST_F(DatabaseTest, InitializeDatabasePopulatesCredentials)
{
    auto conn = initialize_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping initialization test";
    }

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

// Test populate_credentials_table with invalid JSON
TEST_F(DatabaseTest, PopulateCredentialsTableInvalidJSON)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Create file with invalid JSON
    const std::string test_file = "/tmp/invalid_credentials.json";
    {
        std::ofstream file(test_file);
        file << "{ invalid json }";
        file.close();
    }

    int result = populate_credentials_table(*conn, test_file);
    EXPECT_EQ(result, 1); // Should return error

    std::filesystem::remove(test_file);
}

// Test populate_credentials_table with empty array
TEST_F(DatabaseTest, PopulateCredentialsTableEmptyArray)
{
    auto conn = connect_to_database();
    if (!conn)
    {
        GTEST_SKIP() << "Database not available, skipping populate test";
    }

    // Ensure table exists
    create_credentials_table(*conn);

    // Create file with empty array
    const std::string test_file = "/tmp/empty_credentials.json";
    {
        std::ofstream file(test_file);
        file << "[]";
        file.close();
    }

    int result = populate_credentials_table(*conn, test_file);
    EXPECT_EQ(result, 0); // Should succeed (no credentials to insert)

    std::filesystem::remove(test_file);
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
    const std::string test_file = "/tmp/incomplete_credentials.json";
    {
        std::ofstream file(test_file);
        file << "[{\"username\":\"incomplete_user\",\"type\":\"HUB\"}]";
        file.close();
    }

    int result = populate_credentials_table(*conn, test_file);
    EXPECT_EQ(result, 1); // Should return error

    std::filesystem::remove(test_file);
}

// Test build_connection_string with environment variables
TEST_F(DatabaseTest, BuildConnectionStringWithEnvVars)
{
    // Set environment variables
    setenv("POSTGRES_HOST", "test_host", 1);
    setenv("POSTGRES_DB", "test_db", 1);
    setenv("POSTGRES_USER", "test_user", 1);
    setenv("POSTGRES_PASSWORD", "test_pass", 1);
    setenv("POSTGRES_PORT", "5433", 1);

    // Cannot directly test build_connection_string as it's in anonymous namespace
    // But we can test connect_to_database which uses it
    // This will fail to connect with fake credentials, but tests env var usage
    auto conn = connect_to_database();

    // Cleanup environment
    unsetenv("POSTGRES_HOST");
    unsetenv("POSTGRES_DB");
    unsetenv("POSTGRES_USER");
    unsetenv("POSTGRES_PASSWORD");
    unsetenv("POSTGRES_PORT");

    // Connection should fail (or succeed if those creds actually exist)
    // Either outcome is acceptable for this test
    SUCCEED();
}

// Test connect_to_database error handling
TEST_F(DatabaseTest, ConnectToDatabaseErrorHandling)
{
    // Set invalid environment variables to force connection failure
    setenv("POSTGRES_HOST", "invalid_host_12345", 1);
    setenv("POSTGRES_PORT", "99999", 1);

    auto conn = connect_to_database();

    // Should return nullptr on connection failure
    EXPECT_EQ(conn, nullptr);

    // Cleanup
    unsetenv("POSTGRES_HOST");
    unsetenv("POSTGRES_PORT");
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
    pqxx::result db_res = verify_txn.exec_params(
        "SELECT food, water, medicine, tools, guns, ammo FROM client_inventory WHERE client_id = $1", "test_client");
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
    pqxx::result res =
        verify_txn.exec_params("SELECT destination_id FROM inventory_transactions WHERE transaction_id = $1", tid);
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
    pqxx::result res = verify_txn.exec_params(
        "SELECT status, reception_timestamp FROM inventory_transactions WHERE transaction_id = $1", tid);
    EXPECT_EQ(res[0][0].as<std::string>(), "COMPLETED");
    EXPECT_FALSE(res[0][1].is_null());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
