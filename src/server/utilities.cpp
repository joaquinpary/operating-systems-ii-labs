#include "utilities.hpp"

int get_message_code(const std::string& type_str)
{
    if (type_str == "client_keepalive")
        return CLIENT_KEEPALIVE;
    if (type_str == "client_inventory_update")
        return CLIENT_INVENTORY_UPDATE;
    if (type_str == "client_acknowledgment")
        return CLIENT_ACKNOWLEDGMENT;
    if (type_str == "client_infection_alert")
        return CLIENT_INFECTION_ALERT;
    if (type_str == "warehouse_send_stock_to_hub")
        return WAREHOUSE_SEND_STOCK_TO_HUB;
    if (type_str == "warehouse_request_stock")
        return WAREHOUSE_REQUEST_STOCK;
    if (type_str == "hub_request_stock")
        return HUB_REQUEST_STOCK;
    return -1;
}

std::string build_auth_response_json(bool success, const std::string& message)
{
    server_auth_response resp =
        create_server_auth_response(success ? "success" : "failure", success ? "token" : "", message.c_str());
    char* serialized = serialize_server_auth_response(&resp);
    std::string json(serialized);
    free(serialized);
    return json;
}

void copy_response_to_buffer(const std::string& json, std::array<char, DATA_BUFFER_SIZE>& buffer)
{
    std::copy(json.begin(), json.end(), buffer.begin());
}
