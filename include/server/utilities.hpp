#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>

#define CLIENT_KEEPALIVE 0
#define CLIENT_INVENTORY_UPDATE 1
#define CLIENT_ACKNOWLEDGMENT 2
#define CLIENT_INFECTION_ALERT 3
#define WAREHOUSE_SEND_STOCK_TO_HUB 4
#define WAREHOUSE_REQUEST_STOCK 5
#define HUB_REQUEST_STOCK 6

int get_message_code(const std::string& type_str);

#endif