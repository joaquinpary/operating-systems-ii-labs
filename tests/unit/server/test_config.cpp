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
        if (std::filesystem::exists(test_config_path))
        {
            std::filesystem::remove(test_config_path);
        }
        unsetenv("CONFIG_PATH");
    }

    void TearDown() override
    {
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
             << "  \"api_rest_port\": 8080,\n"
             << "  \"ack_timeout\": 5000,\n"
             << "  \"max_auth_attempts\": 3,\n"
             << "  \"max_retries\": 3,\n"
             << "  \"keepalive_timeout\": 120,\n"
             << "  \"pool_size\": 8,\n"
             << "  \"worker_threads\": 4,\n"
             << "  \"credentials_path\": \"config/clients\"\n"
             << "}";
        file.close();
    }
};
TEST_F(ConfigTest, GetEnvVarReturnsDefault)
{
    std::string result = config::get_env_var("NONEXISTENT_VAR", "default_value");
    EXPECT_EQ(result, "default_value");
}
TEST_F(ConfigTest, GetEnvVarReturnsEnvValue)
{
    setenv("TEST_ENV_VAR", "env_value", 1);
    std::string result = config::get_env_var("TEST_ENV_VAR", "default_value");
    EXPECT_EQ(result, "env_value");
    unsetenv("TEST_ENV_VAR");
}
TEST_F(ConfigTest, LoadValidConfig)
{
    create_valid_config_file(test_config_path);

    config::server_config cfg;
    ASSERT_NO_THROW(config::load_config_from_file(test_config_path, cfg));

    EXPECT_EQ(cfg.ip_v4, "127.0.0.1");
    EXPECT_EQ(cfg.ip_v6, "::1");
    EXPECT_EQ(cfg.network_port, 9999);
    EXPECT_EQ(cfg.api_rest_port, 8080);
    EXPECT_EQ(cfg.ack_timeout, 5000);
    EXPECT_EQ(cfg.max_auth_attempts, 3);
    EXPECT_EQ(cfg.max_retries, 3);
    EXPECT_EQ(cfg.keepalive_timeout, 120);
    EXPECT_EQ(cfg.pool_size, 8);
    EXPECT_EQ(cfg.credentials_path, "config/clients");
}
TEST_F(ConfigTest, LoadConfigNonExistentFile)
{
    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file("nonexistent_config.json", cfg), std::runtime_error);
}
TEST_F(ConfigTest, LoadConfigInvalidJSON)
{
    std::ofstream file(test_config_path);
    file << "{ invalid json content }";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}
TEST_F(ConfigTest, LoadConfigMissingIpV4)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"api_rest_port\": 8080,\n"
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
TEST_F(ConfigTest, LoadConfigMissingIpV6)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"api_rest_port\": 8080,\n"
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
TEST_F(ConfigTest, LoadConfigMissingPort)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"api_rest_port\": 8080,\n"
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
TEST_F(ConfigTest, LoadConfigMissingApiRestPort)
{
    std::ofstream file(test_config_path);
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

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}
TEST_F(ConfigTest, LoadConfigMissingAckTimeout)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"api_rest_port\": 8080,\n"
         << "  \"max_auth_attempts\": 3,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}
TEST_F(ConfigTest, LoadConfigMissingMaxAuthAttempts)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"api_rest_port\": 8080,\n"
         << "  \"ack_timeout\": 5000,\n"
         << "  \"max_retries\": 3,\n"
         << "  \"pool_size\": 8,\n"
         << "  \"credentials_path\": \"config/clients\"\n"
         << "}";
    file.close();

    config::server_config cfg;
    EXPECT_THROW(config::load_config_from_file(test_config_path, cfg), std::runtime_error);
}
TEST_F(ConfigTest, LoadConfigInvalidIpV4Type)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": 127,\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": 9999,\n"
         << "  \"api_rest_port\": 8080,\n"
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
TEST_F(ConfigTest, LoadConfigInvalidPortType)
{
    std::ofstream file(test_config_path);
    file << "{\n"
         << "  \"ip_v4\": \"127.0.0.1\",\n"
         << "  \"ip_v6\": \"::1\",\n"
         << "  \"network_port\": \"not_a_number\",\n"
         << "  \"api_rest_port\": 8080,\n"
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
TEST_F(ConfigTest, LoadConfigFromEnvVariable)
{
    const std::string env_config_path = "/tmp/env_config.json";
    create_valid_config_file(env_config_path);

    setenv("CONFIG_PATH", env_config_path.c_str(), 1);

    config::server_config cfg;
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
         << "  \"api_rest_port\": 8080,\n"
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
         << "  \"api_rest_port\": 8080,\n"
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
         << "  \"api_rest_port\": 8080,\n"
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
