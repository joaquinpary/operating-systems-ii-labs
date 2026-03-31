#include "api_rest.hpp"

#include <cstdlib>
#include <chrono>
#include <csignal>
#include <gtest/gtest.h>
#include <httplib.h>
#include <thread>
#include <unistd.h>

class ApiRestTest : public ::testing::Test
{
  protected:
    std::thread server_thread;
    config::server_config cfg;

    void SetUp() override
    {
        cfg.api_rest_port = 18080;
        setenv("MONGO_URI", "mongodb://127.0.0.1:27018/?serverSelectionTimeoutMS=50&connectTimeoutMS=50", 1);
        setenv("MONGO_DB", "test_results", 1);

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);

        server_thread = std::thread([this]() { run_api_rest_process(cfg); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    void TearDown() override
    {
        kill(getpid(), SIGTERM);

        if (server_thread.joinable())
        {
            server_thread.join();
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);

        unsetenv("MONGO_URI");
        unsetenv("MONGO_DB");
    }
};

TEST_F(ApiRestTest, PostMapValidJsonReturns200)
{
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
    EXPECT_TRUE(res->body.find(R"("matrix_size":1)") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("market_1")") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("invalid_node")") == std::string::npos);
}

TEST_F(ApiRestTest, PostMapInvalidJsonReturns400)
{
    httplib::Client cli("localhost", 18080);

    std::string invalid_json = "this is not json";

    auto res = cli.Post("/map", invalid_json, "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    EXPECT_TRUE(res->body.find(R"("status":"error")") != std::string::npos);
}

TEST_F(ApiRestTest, PostFulfillmentCircuitWithoutMapReturns400)
{
    httplib::Client cli("localhost", 18080);

    auto res = cli.Post("/request/fulfillment-circuit", "", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
    EXPECT_TRUE(res->body.find("No map data loaded") != std::string::npos);
}

TEST_F(ApiRestTest, PostFulfillmentCircuitFiltersMarketsAndFindsCircuit)
{
    httplib::Client cli("localhost", 18080);

    std::string map_json = R"([
        {
            "node_id": "FC1",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC2",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "FC2",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC3",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                },
                {
                    "to": "MK1",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "FC3",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC1",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "MK1",
            "node_type": "market",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC1",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        }
    ])";

    auto map_res = cli.Post("/map", map_json, "application/json");
    ASSERT_TRUE(map_res);
    ASSERT_EQ(map_res->status, 200);

    auto circuit_res = cli.Post("/request/fulfillment-circuit", "", "application/json");
    ASSERT_TRUE(circuit_res);
    EXPECT_EQ(circuit_res->status, 200);
    EXPECT_TRUE(circuit_res->body.find(R"("has_circuit":true)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find(R"("node_count":3)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find(R"("timestamp":)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find(R"("use_openmp":)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find("FC1") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find("FC2") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find("FC3") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find("MK1") == std::string::npos);
}

TEST_F(ApiRestTest, PostFulfillmentFlowReturnsTimestamp)
{
    httplib::Client cli("localhost", 18080);

    std::string map_json = R"([
        {
            "node_id": "FC1",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC2",
                    "connection_type": "road",
                    "base_weight": 5.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "FC2",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": []
        }
    ])";

    auto map_res = cli.Post("/map", map_json, "application/json");
    ASSERT_TRUE(map_res);
    ASSERT_EQ(map_res->status, 200);

    auto flow_res = cli.Post("/request/fulfillment-flow", R"({"source":"FC1","sink":"FC2"})", "application/json");
    ASSERT_TRUE(flow_res);
    EXPECT_EQ(flow_res->status, 200);
    EXPECT_TRUE(flow_res->body.find(R"("status":"ok")") != std::string::npos);
    EXPECT_TRUE(flow_res->body.find(R"("node_count":2)") != std::string::npos);
    EXPECT_TRUE(flow_res->body.find(R"("max_flow":5.00)") != std::string::npos);
    EXPECT_TRUE(flow_res->body.find(R"("timestamp":)") != std::string::npos);
    EXPECT_TRUE(flow_res->body.find(R"("use_openmp":)") != std::string::npos);
}

TEST_F(ApiRestTest, PostFulfillmentCircuitSupportsStartNode)
{
    httplib::Client cli("localhost", 18080);

    std::string map_json = R"([
        {
            "node_id": "FC1",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC2",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "FC2",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC3",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "FC3",
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true,
            "connections": [
                {
                    "to": "FC1",
                    "connection_type": "road",
                    "base_weight": 1.0,
                    "connection_conditions": []
                }
            ]
        }
    ])";

    auto map_res = cli.Post("/map", map_json, "application/json");
    ASSERT_TRUE(map_res);
    ASSERT_EQ(map_res->status, 200);

    auto circuit_res = cli.Post("/request/fulfillment-circuit", R"({"start":"FC2"})", "application/json");
    ASSERT_TRUE(circuit_res);
    EXPECT_EQ(circuit_res->status, 200);
    EXPECT_TRUE(circuit_res->body.find(R"("timestamp":)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find(R"("use_openmp":)") != std::string::npos);
    EXPECT_TRUE(circuit_res->body.find(R"(["FC2","FC3","FC1","FC2"])") != std::string::npos);
}

TEST_F(ApiRestTest, GetResultsReturns200WithJsonArray)
{
    httplib::Client cli("localhost", 18080);

    auto res = cli.Get("/results");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_TRUE(res->body.find(R"("status":"ok")") != std::string::npos);
    EXPECT_TRUE(res->body.find(R"("results":[)") != std::string::npos);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
