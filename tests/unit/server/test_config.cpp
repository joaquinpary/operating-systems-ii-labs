#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

class ConfigTest : public ::testing::Test
{
  protected:
    const std::string test_config_path = "/tmp/test_server_config.json";
    
    void SetUp() override
    {
        // Clean up any existing test files
        if (std::filesystem::exists(test_config_path))
        {
            std::filesystem::remove(test_config_path);
        }
        
        // Clear environment variable
        unsetenv("CONFIG_PATH");
    }

    void TearDown() override
    {
        // Clean up test files
        if (std::filesystem::exists(test_config_path))
        {
            std::filesystem::remove(test_config_path);
        }
        unsetenv("CONFIG_PATH");
    }

    void create_valid_config_file(const std::string& path)
    {
        std::ofstream file(path);
        file << "{\n"
             << "  \"ip_v4\": \"127.0.0.1\",\n"
             << "  \"ip_v6\": \"::1\",\n"
             << "  \"network_port\": 9999,\n"
             << "  \"ack_timeout\": 5000,\n"
             << "  \"max_auth_attempts\": 3,\n"
             << "  \"max_retries\": 3,\n"
             << "  \"pool_size\": 8,\n"
             << "  \"credentials_path\": \"config/clients\"\n"
             << "}";
        file.close();
    }
};

// Test get_env_var with no environment variable set
TEST_F(ConfigTest, GetEnvVarReturnsDefault)
{
    std::string result = config::get_env_var("NONEXISTENT_VAR", "default_value");
    EXPECT_EQ(result, "default_value");
}

// Test get_env_var with environment variable set
TEST_F(ConfigTest, GetEnvVarReturnsEnvValue)
{
    setenv("TEST_ENV_VAR", "env_value", 1);
    std::string result = config::get_env_var("TEST_ENV_VAR", "default_value");
    EXPECT_EQ(result, "env_value");
    unsetenv("TEST_ENV_VAR");
}

// Test load_config_from_file with valid config
TEST_F(ConfigTest, LoadValidConfig)
{
    create_valid_config_file(test_config_path);
    
    config::server_config cfg;
    ASSERT_NO_THROW(config::load_config_from_file(test_config_path, cfg));
    
    EXPECT_EQ(cfg.ip_v4, "127.0.0.1");
    EXPECT_EQ(cfg.ip_v6, "::1");
    EXPECT_EQ(cfg.network_port, 9999);
    EXPECT_EQ(cfg.ack_timeout, 5000);
    EXPECT_EQ(cfg.max_auth_attempts, 3);
    EXPECT_EQ(cfg.max_retries, 3);
    EXPECT_EQ(cfg.pool_size, 8);
    EXPECT_EQ(cfg.credentials_path, "config/clients");
}

// Test load_config_from_file with non-existent file
TEST_F(ConfigTest, LoadConfigNonExistentFile)
{
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file("nonexistent_config.json", cfg), std::runtime_error);
}

// Test load_config_from_file with invalid JSON
TEST_F(ConfigTest, LoadConfigInvalidJSON)
{
    std::ofstream file(test_config_path);
    file << "{ invalid json content }";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with missing ip_v4
TEST_F(ConfigTest, LoadConfigMissingIpV4)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with missing ip_v6
TEST_F(ConfigTest, LoadConfigMissingIpV6)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with missing network_port
TEST_F(ConfigTest, LoadConfigMissingPort)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with missing ack_timeout
TEST_F(ConfigTest, LoadConfigMissingAckTimeout)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with missing max_auth_attempts
TEST_F(ConfigTest, LoadConfigMissingMaxAuthAttempts)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with invalid ip_v4 type
TEST_F(ConfigTest, LoadConfigInvalidIpV4Type)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": 127,\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with invalid port type
TEST_F(ConfigTest, LoadConfigInvalidPortType)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": \"not_a_number\",\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();
    
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

// Test load_config_from_file with CONFIG_PATH environment variable
TEST_F(ConfigTest, LoadConfigFromEnvVariable)
{
    const std::string env_config_path = "/tmp/env_config.json";
    create_valid_config_file(env_config_path);
    
    setenv("CONFIG_PATH", env_config_path.c_str(), 1);
    
    config::server_config cfg;
    // Even though we pass test_config_path, it should use env variable path
    ASSERT_NO_THROW(config::load_config_from_file("ignored_path.json", cfg));
    
    EXPECT_EQ(cfg.ip_v4, "127.0.0.1");
    
    unsetenv("CONFIG_PATH");
    std::filesystem::remove(env_config_path);
}

TEST_F(ConfigTest, LoadConfigMissingMaxRetries)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

TEST_F(ConfigTest, LoadConfigMissingPoolSize)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

TEST_F(ConfigTest, LoadConfigMissingCredentialsPath)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8\n"
         << "}";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

