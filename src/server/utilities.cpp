#include "utilities.hpp"
#include <iostream>

#define ITEM_COUNT 6

int get_message_code(const std::string& type_str)
{
    if (type_str == "client_keepalive")
        return CLIENT_KEEPALIVE;
    if (type_str == "client_inventory_update")
        return CLIENT_INVENTORY_UPDATE;
    if (type_str == "client_acknowledgment")
        return CLIENT_ACKNOWLEDGEMENT;
    if (type_str == "client_infection_alert")
        return CLIENT_INFECTION_ALERT;
    if (type_str == "warehouse_send_stock_to_hub")
        return WAREHOUSE_SEND_STOCK_TO_HUB;
    if (type_str == "warehouse_request_stock")
        return WAREHOUSE_REQUEST_STOCK;
    if (type_str == "hub_request_stock")
        return HUB_REQUEST_STOCK;
    if (type_str == "hub_confirm_stock")
        return HUB_CONFIRM_STOCK;
    return -1;
}

std::string build_auth_response_json(bool success, const std::string& message)
{
    std::cout << "Building auth response json for: " << (success ? "success" : "failure") << std::endl;
    server_auth_response resp =
        create_server_auth_response(success ? "success" : "failure", success ? "token" : "", message.c_str());
    char* serialized = serialize_server_auth_response(&resp);
    std::string json(serialized);
    free(serialized);
    return json;
}

std::string build_stock_shipment_notify_json(warehouse_send_stock_to_hub stock_shipment_msg)
{
    server_h_send_stock stock_shipment_notification =
        create_server_h_send_stock(stock_shipment_msg.payload.username, stock_shipment_msg.payload.items, ITEM_COUNT);
    char* serialized = serialize_server_h_send_stock(&stock_shipment_notification, ITEM_COUNT);
    std::string json(serialized);
    free(serialized);
    return json;
}

std::string build_placed_order_json(hub_request_stock placed_order_msg)
{
    server_w_stock_hub new_order_notification =
        create_server_w_stock_hub(placed_order_msg.payload.username, placed_order_msg.payload.items, ITEM_COUNT);
    char* serialized = serialize_server_w_stock_hub(&new_order_notification, ITEM_COUNT);
    std::string json(serialized);
    free(serialized);
    return json;
}

std::string build_stock_warehouse_json(warehouse_request_stock warehouse_stock_request)
{
    server_w_stock_warehouse stock_warehouse =
        create_server_w_stock_warehouse(warehouse_stock_request.payload.items, ITEM_COUNT);

    std::cout << "Created stock warehouse object" << std::endl;

    char* serialized = serialize_server_w_stock_warehouse(&stock_warehouse, ITEM_COUNT);
    if (!serialized)
    {
        std::cerr << "Error: Failed to serialize stock warehouse" << std::endl;
        return "";
    }

    std::string json(serialized);
    free(serialized);
    std::cout << "Stock warehouse: " << json << "\n" << std::flush;
    return json;
}

void copy_response_to_buffer(const std::string& json, std::array<char, DATA_BUFFER_SIZE>& buffer)
{
    std::copy(json.begin(), json.end(), buffer.begin());
}
