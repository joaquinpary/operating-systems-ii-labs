#include <gtest/gtest.h>
#include "database.hpp"
#include <cstdlib>
#include <fstream>
#include <filesystem>


void write_test_credentials_json() {
    const std::string json = R"({
        "clients": [
            {
                "client_id": "W001",
                "username": "user0001",
                "password": "iROEZx51Kt"
            }
        ]
    })";
    std::filesystem::create_directories("config");
    std::ofstream out("config/server_credentials.json");
    ASSERT_TRUE(out.is_open()) << "No se pudo escribir server_credentials.json";
    out << json;
    out.close();
}
// Helpers para preparar entorno de prueba
void prepare_env_vars() {
    setenv("POSTGRES_DB", "dhl_test_db", 1);
    setenv("POSTGRES_USER", "dhl_user", 1);
    setenv("POSTGRES_PASSWORD", "dhl_pass", 1);
    setenv("POSTGRES_HOST", "localhost", 1);

}


// Fixture para limpiar y preparar antes de cada test
class DatabaseManagerTest : public ::testing::Test {
    protected:
        void SetUp() override {
            prepare_env_vars();
            write_test_credentials_json();
            db = std::make_unique<database_manager>();
        }
    
        std::unique_ptr<database_manager> db;
    };
    

// TESTS -----------------------------

TEST_F(DatabaseManagerTest, AuthenticateValidClient) {
    EXPECT_TRUE(db->authenticate_client("user0001", "iROEZx51Kt"));
}

TEST_F(DatabaseManagerTest, AuthenticateInvalidPassword) {
    EXPECT_FALSE(db->authenticate_client("user0001", "wrongpass"));
}

TEST_F(DatabaseManagerTest, AuthenticateNonExistentUser) {
    EXPECT_FALSE(db->authenticate_client("nonexistent", "iROEZx51Kt"));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
