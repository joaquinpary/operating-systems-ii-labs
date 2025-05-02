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
