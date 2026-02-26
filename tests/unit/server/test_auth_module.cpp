#include <gtest/gtest.h>
#include <pqxx/pqxx>
#include <server/auth_module.hpp>
#include <server/database.hpp>

class AuthModuleTest : public ::testing::Test
{
  protected:
    std::unique_ptr<pqxx::connection> db_conn;
    std::unique_ptr<auth_module> auth_mod;

    void SetUp() override
    {
        db_conn = connect_to_database();
        if (!db_conn)
        {
            GTEST_SKIP() << "Database not available, skipping auth_module tests";
        }

        create_credentials_table(*db_conn);

        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM credentials");
            txn.commit();
        }

        {
            pqxx::work txn(*db_conn);
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('test_user', 'correct_hash', 'HUB', TRUE)");
            txn.exec("INSERT INTO credentials (username, password_hash, client_type, is_active) "
                     "VALUES ('inactive_user', 'hash123', 'WAREHOUSE', FALSE)");
            txn.commit();
        }

        auth_mod = std::make_unique<auth_module>(*db_conn);
    }

    void TearDown() override
    {
        auth_mod.reset();
        if (db_conn)
        {
            pqxx::work txn(*db_conn);
            txn.exec("DELETE FROM credentials WHERE username LIKE 'test_%' OR username = 'inactive_user'");
            txn.commit();
        }
        db_conn.reset();
    }
};

TEST_F(AuthModuleTest, AuthenticateWithCorrectCredentials)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("test_user", "correct_hash");

    EXPECT_EQ(result.status_code, auth_result_code::SUCCESS);
    EXPECT_EQ(result.client_type, "HUB");
    EXPECT_EQ(result.username, "test_user");
    EXPECT_TRUE(result.error_message.empty());
}

TEST_F(AuthModuleTest, AuthenticateWithWrongPassword)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("test_user", "wrong_hash");

    EXPECT_EQ(result.status_code, auth_result_code::INVALID_CREDENTIALS);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(AuthModuleTest, AuthenticateWithNonExistentUser)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("non_existent_user", "any_hash");

    EXPECT_EQ(result.status_code, auth_result_code::INVALID_CREDENTIALS);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(AuthModuleTest, AuthenticateWithInactiveUser)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("inactive_user", "hash123");

    EXPECT_EQ(result.status_code, auth_result_code::USER_INACTIVE);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(AuthModuleTest, AuthenticateWithEmptyUsername)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("", "any_hash");

    EXPECT_EQ(result.status_code, auth_result_code::INVALID_CREDENTIALS);
}

TEST_F(AuthModuleTest, AuthenticateWithEmptyPassword)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result = auth_mod->authenticate("test_user", "");

    EXPECT_EQ(result.status_code, auth_result_code::INVALID_CREDENTIALS);
}

TEST_F(AuthModuleTest, MultipleAuthenticationAttempts)
{
    if (!db_conn)
    {
        GTEST_SKIP() << "Database not available";
    }

    auth_result result1 = auth_mod->authenticate("test_user", "wrong_hash");
    EXPECT_EQ(result1.status_code, auth_result_code::INVALID_CREDENTIALS);

    auth_result result2 = auth_mod->authenticate("test_user", "wrong_hash");
    EXPECT_EQ(result2.status_code, auth_result_code::INVALID_CREDENTIALS);

    auth_result result3 = auth_mod->authenticate("test_user", "correct_hash");
    EXPECT_EQ(result3.status_code, auth_result_code::SUCCESS);
}
