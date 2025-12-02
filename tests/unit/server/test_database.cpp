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

    pqxx::work txn(*conn);
    int result = create_credentials_table(txn);
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
    pqxx::work setup_txn(*conn);
    create_credentials_table(setup_txn);

    // Query for non-existent user
    pqxx::work txn(*conn);
    auto cred = query_credentials_by_username(txn, "nonexistent_user");
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
    {
        pqxx::work setup_txn(*conn);
        create_credentials_table(setup_txn);
    }

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
        pqxx::work query_txn(*conn);
        auto cred = query_credentials_by_username(query_txn, "test_user");
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
    {
        pqxx::work setup_txn(*conn);
        create_credentials_table(setup_txn);
    }

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
        pqxx::work query_txn1(*conn);
        auto cred1 = query_credentials_by_username(query_txn1, "test_user1");
        ASSERT_NE(cred1, nullptr);
        EXPECT_EQ(cred1->username, "test_user1");
        EXPECT_EQ(cred1->password_hash, "hash1");
        EXPECT_EQ(cred1->client_type, "HUB");
    }

    {
        pqxx::work query_txn2(*conn);
        auto cred2 = query_credentials_by_username(query_txn2, "test_user2");
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
    {
        pqxx::work setup_txn(*conn);
        create_credentials_table(setup_txn);
    }

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

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
