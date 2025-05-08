#ifndef UTILITIES_H
#define UTILITIES_H

#include "json_manager.h"
#include <array>
#include <string>

#define CLIENT_KEEPALIVE 0
#define CLIENT_INVENTORY_UPDATE 1
#define CLIENT_ACKNOWLEDGMENT 2
#define CLIENT_INFECTION_ALERT 3
#define WAREHOUSE_SEND_STOCK_TO_HUB 4
#define WAREHOUSE_REQUEST_STOCK 5
#define HUB_REQUEST_STOCK 6

const size_t DATA_BUFFER_SIZE = 1024;

int get_message_code(const std::string& type_str);

std::string build_auth_response_json(bool success, const std::string& message);

void copy_response_to_buffer(const std::string& json, std::array<char, DATA_BUFFER_SIZE>& buffer);

#endif
