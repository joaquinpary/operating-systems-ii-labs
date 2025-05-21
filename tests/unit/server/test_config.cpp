#include "config.hpp"
#include <cstdio> // std::remove
#include <fstream>
#include <gtest/gtest.h>

void write_temp_file(const std::string& filename, const std::string& content)
{
    std::ofstream out(filename);
    ASSERT_TRUE(out.is_open());
    out << content;
    out.close();
}

TEST(ConfigTest, LoadValidConfig)
{
    std::string filename = "test_config.json";
    std::string json = R"({
        "ip_v4": "127.0.0.1",
        "ip_v6": "::1",
        "port_tcp_v4": 8080,
        "port_tcp_v6": 8081,
        "port_udp_v4": 9090,
        "port_udp_v6": 9091,
        "ack_timeout": 10,
        "max_auth_attempts": 3,
        "max_auth_attempts_map_size": 100
    })";

    write_temp_file(filename, json);

    config cfg = config::load_config_from_file(filename);

    EXPECT_EQ(cfg.ip_v4, "127.0.0.1");
    EXPECT_EQ(cfg.ip_v6, "::1");
    EXPECT_EQ(cfg.port_tcp_v4, 8080);
    EXPECT_EQ(cfg.port_tcp_v6, 8081);
    EXPECT_EQ(cfg.port_udp_v4, 9090);
    EXPECT_EQ(cfg.port_udp_v6, 9091);
    EXPECT_EQ(cfg.ack_timeout, 10);
    EXPECT_EQ(cfg.max_auth_attempts, 3);
    EXPECT_EQ(cfg.max_auth_attempts_map_size, 100);

    std::remove(filename.c_str());
}

TEST(ConfigTest, LoadInvalidJson)
{
    std::string filename = "test_invalid_config.json";
    std::string json = R"({ "ip_v4": "127.0.0.1", "port_tcp_v4": "not-a-number" })";
    write_temp_file(filename, json);

    config cfg = config::load_config_from_file(filename);

    if (cfg.port_tcp_v4 < 0 || cfg.port_tcp_v4 > 65535)
    {
        cfg.port_tcp_v4 = 0;
    }

    EXPECT_EQ(cfg.ip_v4, "");
    EXPECT_EQ(cfg.port_tcp_v4, 0);

    std::remove(filename.c_str());
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
