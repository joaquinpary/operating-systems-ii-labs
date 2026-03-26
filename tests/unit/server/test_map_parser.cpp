#include <gtest/gtest.h>
#include "map_parser.hpp"

namespace server 
{
namespace
{

TEST(MapParserTest, EmptyArray)
{
    std::string json = "[]";
    MapParseResult result = parse_map_json(json);
    
    EXPECT_EQ(result.total_received, 0);
    EXPECT_EQ(result.total_discarded, 0);
    EXPECT_TRUE(result.nodes.empty());
}

TEST(MapParserTest, InvalidJson)
{
    std::string json = "{ malformed ";
    EXPECT_THROW(parse_map_json(json), std::runtime_error);
}

TEST(MapParserTest, FilterType)
{
    std::string json = R"([
        {
            "node_type": "unknown",
            "is_active": true,
            "is_secure": true
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 1);
    EXPECT_EQ(result.total_discarded, 1);
    EXPECT_TRUE(result.nodes.empty());
}

TEST(MapParserTest, FilterInactive)
{
    std::string json = R"([
        {
            "node_type": "fulfillment_center",
            "is_active": false,
            "is_secure": true
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 1);
    EXPECT_EQ(result.total_discarded, 1);
    EXPECT_TRUE(result.nodes.empty());
}

TEST(MapParserTest, FilterInsecure)
{
    std::string json = R"([
        {
            "node_type": "market",
            "is_active": true,
            "is_secure": false
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 1);
    EXPECT_EQ(result.total_discarded, 1);
    EXPECT_TRUE(result.nodes.empty());
}

TEST(MapParserTest, AcceptValidFulfillmentCenter)
{
    std::string json = R"([
        {
            "node_id": "N001",
            "node_name": "East Fulfillment Center",
            "node_type": "fulfillment_center",
            "node_description": "Short narrative for human identification",
            "node_location": {
                "latitude": 40.7128,
                "longitude": -74.006
            },
            "node_tags": [
                "food",
                "ammo"
            ],
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "to": "N002",
                    "connection_type": "road",
                    "connection_conditions": [
                        "infected_activity"
                    ]
                }
            ],
            "schema": {
                "version": 1
            }
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 1);
    EXPECT_EQ(result.total_discarded, 0);
    ASSERT_EQ(result.nodes.size(), 1);
    
    const MapNode& node = result.nodes[0];
    EXPECT_EQ(node.node_id, "N001");
    EXPECT_EQ(node.node_name, "East Fulfillment Center");
    EXPECT_EQ(node.node_type, "fulfillment_center");
    EXPECT_EQ(node.node_description, "Short narrative for human identification");
    EXPECT_DOUBLE_EQ(node.location.latitude, 40.7128);
    EXPECT_DOUBLE_EQ(node.location.longitude, -74.006);
    EXPECT_TRUE(node.is_secure);
    EXPECT_TRUE(node.is_active);
    
    ASSERT_EQ(node.node_tags.size(), 2);
    EXPECT_EQ(node.node_tags[0], "food");
    EXPECT_EQ(node.node_tags[1], "ammo");
    
    ASSERT_EQ(node.connections.size(), 1);
    const MapEdge& edge = node.connections[0];
    EXPECT_EQ(edge.to, "N002");
    EXPECT_EQ(edge.connection_type, "road");
    ASSERT_EQ(edge.connection_conditions.size(), 1);
    EXPECT_EQ(edge.connection_conditions[0], "infected_activity");
}

TEST(MapParserTest, AcceptValidMarket)
{
    std::string json = R"([
        {
            "node_id": "M001",
            "node_type": "market",
            "is_secure": true,
            "is_active": true
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 1);
    EXPECT_EQ(result.total_discarded, 0);
    ASSERT_EQ(result.nodes.size(), 1);
    EXPECT_EQ(result.nodes[0].node_id, "M001");
    EXPECT_EQ(result.nodes[0].node_type, "market");
}

TEST(MapParserTest, MixedBatch)
{
    std::string json = R"([
        {
            "node_type": "fulfillment_center",
            "is_active": true,
            "is_secure": true
        },
        {
            "node_type": "unknown",
            "is_active": true,
            "is_secure": true
        },
        {
            "node_type": "market",
            "is_active": false,
            "is_secure": true
        },
        {
            "node_type": "market",
            "is_active": true,
            "is_secure": true
        }
    ])";
    
    MapParseResult result = parse_map_json(json);
    EXPECT_EQ(result.total_received, 4);
    EXPECT_EQ(result.total_discarded, 2);
    ASSERT_EQ(result.nodes.size(), 2);
    EXPECT_EQ(result.nodes[0].node_type, "fulfillment_center");
    EXPECT_EQ(result.nodes[1].node_type, "market");
}

} // namespace
} // namespace server
