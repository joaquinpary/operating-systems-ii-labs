#include "message_handler.h"
#include "json_manager.h"
#include "logger.h"
#include "shared_state.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Helper function to create and enqueue an ACK message
 * @param msg_timestamp The timestamp of the message being acknowledged
 * @return 0 on success, -1 on failure
 */
static int send_ack_to_server(const char* msg_timestamp)
{
    shared_data_t* shared_data = get_shared_data();
    message_t ack_msg;

    if (create_acknowledgment_message(&ack_msg, shared_data->client_role, shared_data->client_id, SERVER, SERVER,
                                      msg_timestamp, 200) != 0)
    {
        LOG_ERROR_MSG("Failed to create ACK message");
        fprintf(stderr, "[RECEIVER] Failed to create ACK message\n");
        return -1;
    }

    if (enqueue_pending_message(&ack_msg) != 0)
    {
        LOG_ERROR_MSG("Failed to enqueue ACK");
        fprintf(stderr, "[RECEIVER] Failed to enqueue ACK\n");
        return -1;
    }

    LOG_DEBUG_MSG("ACK enqueued successfully");
    return 0;
}

int handle_server_message(const message_t* msg)
{
    if (!msg)
    {
        LOG_ERROR_MSG("Null message pointer");
        return -1;
    }

    shared_data_t* shared_data = get_shared_data();

    printf("[RECEIVER] Got message type: %s\n", msg->msg_type);

    // Handle ACK messages - just remove from pending
    if (strstr(msg->msg_type, "ACK") != NULL)
    {
        remove_pending_ack(msg->payload.acknowledgment.ack_for_timestamp);
        return 0;
    }

    // Handle AUTH_RESPONSE - send ACK
    if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__AUTH_RESPONSE) == 0 ||
        strcmp(msg->msg_type, SERVER_TO_HUB__AUTH_RESPONSE) == 0)
    {
        if (send_ack_to_server(msg->timestamp) != 0)
        {
            return -1;
        }

        LOG_DEBUG_MSG("ACK enqueued for message type: %s", msg->msg_type);
        return 0;
    }

    // Handle INVENTORY_UPDATE - add to inventory and send ACK
    if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__INVENTORY_UPDATE) == 0 ||
        strcmp(msg->msg_type, SERVER_TO_HUB__INVENTORY_UPDATE) == 0)
    {
        if (modify_inventory(msg->payload.inventory_update.items, INVENTORY_ADD) != 0)
        {
            LOG_ERROR_MSG("Failed to update inventory from server message");
            fprintf(stderr, "[RECEIVER] Failed to update inventory from server message\n");
            return -1;
        }

        if (send_ack_to_server(msg->timestamp) != 0)
        {
            return -1;
        }

        LOG_DEBUG_MSG("ACK enqueued for INVENTORY_UPDATE message");
        return 0;
    }

    // Handle ORDER_TO_DISPATCH (WAREHOUSE only)
    if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__ORDER_TO_DISPATCH_STOCK_TO_HUB) == 0)
    {
        LOG_INFO_MSG("Received dispatch order to HUB: %s", msg->target_id);

        // Send ACK to server
        if (send_ack_to_server(msg->timestamp) != 0)
        {
            return -1;
        }
        LOG_DEBUG_MSG("ACK sent for dispatch order");

        // Reduce inventory
        const payload_order_stock* order = &msg->payload.order_stock;
        if (modify_inventory(order->items, INVENTORY_REDUCE) != 0)
        {
            LOG_ERROR_MSG("Failed to reduce inventory for dispatch order");
            return -1;
        }

        // Send shipment notice
        message_t shipment_msg;
        if (create_items_message(&shipment_msg, WAREHOUSE_TO_SERVER__SHIPMENT_NOTICE, shared_data->client_id,
                                 msg->target_id, // Forward to target HUB
                                 order->items, QUANTITY_ITEMS, NULL) != 0)
        {
            LOG_ERROR_MSG("Failed to create shipment notice message");
            return -1;
        }

        if (enqueue_pending_message(&shipment_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue shipment notice");
            return -1;
        }
        LOG_INFO_MSG("Shipment notice sent, dispatching to HUB %s", msg->target_id);
        return 0;
    }

    // Handle RESTOCK_NOTICE and INCOMING_STOCK_NOTICE
    if (strcmp(msg->msg_type, SERVER_TO_WAREHOUSE__RESTOCK_NOTICE) == 0 ||
        strcmp(msg->msg_type, SERVER_TO_HUB__INCOMING_STOCK_NOTICE) == 0)
    {
        LOG_INFO_MSG("Received stock delivery from server");

        // Send ACK to server
        if (send_ack_to_server(msg->timestamp) != 0)
        {
            return -1;
        }
        LOG_DEBUG_MSG("ACK sent for stock delivery");

        // Add inventory (stock from server)
        const payload_restock_notice* restock = &msg->payload.restock_notice;
        if (modify_inventory(restock->items, INVENTORY_ADD) != 0)
        {
            LOG_ERROR_MSG("Failed to add inventory for stock delivery");
            return -1;
        }

        clear_pending_stock_requests(restock->items, QUANTITY_ITEMS);

        // Send receipt confirmation (role-specific message type)
        message_t receipt_msg;
        const char* receipt_type = (strcmp(shared_data->client_role, HUB) == 0)
                                       ? HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION
                                       : WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION;

        if (create_items_message(&receipt_msg, receipt_type, shared_data->client_id, SERVER, restock->items,
                                 QUANTITY_ITEMS, msg->timestamp) != 0)
        {
            LOG_ERROR_MSG("Failed to create receipt confirmation message");
            return -1;
        }

        if (enqueue_pending_message(&receipt_msg) != 0)
        {
            LOG_ERROR_MSG("Failed to enqueue receipt confirmation");
            return -1;
        }
        LOG_INFO_MSG("Stock delivery processed and receipt confirmation sent");
        return 0;
    }

    // Handle SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT - log it and send ACK
    if (strcmp(msg->msg_type, SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT) == 0)
    {
        const payload_server_emergency_alert* alert = &msg->payload.server_emergency;
        LOG_WARNING_MSG("Emergency broadcast received from server: code=%d instructions='%s'", alert->emergency_code,
                        alert->instructions);

        if (send_ack_to_server(msg->timestamp) != 0)
        {
            return -1;
        }

        LOG_DEBUG_MSG("ACK sent for SERVER_TO_ALL_CLIENTS__EMERGENCY_ALERT");
        return 0;
    }

    LOG_WARNING_MSG("Unknown message type: %s", msg->msg_type);
    return 0;
}
