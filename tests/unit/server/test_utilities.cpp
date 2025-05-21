#include "utilities.hpp"
#include "gtest/gtest.h"

TEST(GetMessageCodeTest, ValidCodes)
{
    EXPECT_EQ(get_message_code("client_keepalive"), CLIENT_KEEPALIVE);
    EXPECT_EQ(get_message_code("client_inventory_update"), CLIENT_INVENTORY_UPDATE);
    EXPECT_EQ(get_message_code("client_acknowledgment"), CLIENT_ACKNOWLEDGMENT);
    EXPECT_EQ(get_message_code("client_infection_alert"), CLIENT_INFECTION_ALERT);
    EXPECT_EQ(get_message_code("warehouse_send_stock_to_hub"), WAREHOUSE_SEND_STOCK_TO_HUB);
    EXPECT_EQ(get_message_code("warehouse_request_stock"), WAREHOUSE_REQUEST_STOCK);
    EXPECT_EQ(get_message_code("hub_request_stock"), HUB_REQUEST_STOCK);
}

TEST(GetMessageCodeTest, InvalidCode)
{
    EXPECT_EQ(get_message_code("unknown_type"), -1);
    EXPECT_EQ(get_message_code(""), -1);
    EXPECT_EQ(get_message_code("CLIENT_KEEPALIVE"), -1); // case-sensitive
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
