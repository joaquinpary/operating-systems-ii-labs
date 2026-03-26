#include <gtest/gtest.h>
#include <httplib.h>
#include "api_rest.hpp"
#include <thread>
#include <chrono>
#include <csignal>
#include <unistd.h>

class ApiRestTest : public ::testing::Test {
protected:
    std::thread server_thread;
    config::server_config cfg;

    void SetUp() override {
        // Use an arbitrary port for testing to avoid conflicts
        cfg.api_rest_port = 18080;

        // Block SIGTERM in main thread so that only the signal waiter thread inside 
        // run_api_rest_process catches it.
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);

        // Start REST server in background
        server_thread = std::thread([this]() {
            run_api_rest_process(cfg);
        });

        // Give the server a moment to start and bind
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override {
        // Send SIGTERM to the process so that the `sigwait` thread inside the server stops it.
        kill(getpid(), SIGTERM);
        
        if (server_thread.joinable()) {
            server_thread.join();
        }

        // Unblock SIGTERM
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
    }
};

TEST_F(ApiRestTest, PostMapValidJsonReturns200) {
    httplib::Client cli("localhost", 18080);
    
    std::string valid_json = R"([
        {
            "node_id": "market_1",
            "node_type": "market",
            "is_active": true,
            "is_secure": true,
            "node_name": "Central Market",
            "node_description": "Main hub",
            "location": {"latitude": 10.0, "longitude": 20.0},
            "node_tags": [],
            "connections": []
        },
        {
            "node_id": "invalid_node",
            "node_type": "unknown",
            "is_active": true,
            "is_secure": true,
            "location": {"latitude": 0.0, "longitude": 0.0}
        }
    ])";
    
    auto res = cli.Post("/map", valid_json, "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_TRUE(res->body.find(R"("status":"ok")") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("accepted":1)") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("discarded":1)") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("market_1")") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("invalid_node")") == std::string::npos);
}

TEST_F(ApiRestTest, PostMapInvalidJsonReturns400) {
    httplib::Client cli("localhost", 18080);
    
    std::string invalid_json = "this is not json";
    
    auto res = cli.Post("/map", invalid_json, "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    EXPECT_TRUE(res->body.find(R"("status":"error")") != std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
