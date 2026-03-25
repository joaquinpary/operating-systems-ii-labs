#ifndef MESSAGE_HANDLER_H
#define MESSAGE_HANDLER_H

#include "json_manager.h"

/**
 * @brief Handle incoming server message and perform appropriate actions
 *
 * Processes different message types from the server:
 * - ACK: Remove pending acknowledgment
 * - AUTH_RESPONSE: Send ACK back to server
 * - INVENTORY_UPDATE: Update local inventory and send ACK
 * - ORDER_TO_DISPATCH: Reduce inventory, send ACK and shipment notice
 * - RESTOCK_NOTICE/INCOMING_STOCK: Add inventory, send ACK and receipt confirmation
 *
 * @param msg Pointer to the received message
 * @return 0 on success, -1 on error
 */
int handle_server_message(const message_t* msg);

#endif
