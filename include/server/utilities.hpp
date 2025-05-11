#ifndef UTILITIES_H
#define UTILITIES_H

#include "json_manager.h"
#include <array>
#include <string>

#define CLIENT_KEEPALIVE 0
#define CLIENT_INVENTORY_UPDATE 1
#define CLIENT_ACKNOWLEDGEMENT 2
#define CLIENT_INFECTION_ALERT 3
#define WAREHOUSE_SEND_STOCK_TO_HUB 4
#define WAREHOUSE_REQUEST_STOCK 5
#define HUB_REQUEST_STOCK 6

const size_t DATA_BUFFER_SIZE = 1024;

int get_message_code(const std::string& type_str);

std::string build_auth_response_json(bool success, const std::string& message);
std::string build_stock_shipment_notify_json(warehouse_send_stock_to_hub stock_shipment_msg);
std::string build_placed_order_json(hub_request_stock placed_order_msg);
std::string build_stock_warehouse_json(warehouse_request_stock warehouse_stock_request);



void copy_response_to_buffer(const std::string& json, std::array<char, DATA_BUFFER_SIZE>& buffer);

#endif
