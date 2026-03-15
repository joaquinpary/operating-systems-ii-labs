#include "message_handler.h"
#include "timers.h"
#include "json_manager.h"
#include "logger.h"
#include "shared_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void random_response_delay(void)
{
    const timer_config_t* cfg = get_timer_config();
    static int seeded = 0;
    if (!seeded)
    {
        srand((unsigned)(time(NULL) ^ getpid()));
        seeded = 1;
    }
    int delay_ms = cfg->response_delay_min_ms + (rand() % (cfg->response_delay_max_ms - cfg->response_delay_min_ms + 1));
    struct timespec ts = {0, (long)delay_ms * 1000000L};
    nanosleep(&ts, NULL);
}

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

        log_inventory_snapshot("INVENTORY_UPDATE recv");

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

        // Delay before processing dispatch to avoid burst traffic
        random_response_delay();

        // Reduce inventory
        const payload_order_stock* order = &msg->payload.order_stock;
        if (modify_inventory(order->items, INVENTORY_REDUCE) != 0)
        {
            LOG_ERROR_MSG("Failed to reduce inventory for dispatch order");
            return -1;
        }

        log_inventory_snapshot("ORDER_TO_DISPATCH recv");

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

        // Delay before checking replenishment to avoid burst traffic
        random_response_delay();

        // Check if dispatch left warehouse below stock threshold
        inventory_item_t low_items[QUANTITY_ITEMS];
        int low_indices[QUANTITY_ITEMS];
        int critical_count = 0;

        const timer_config_t* cfg = get_timer_config();
        int low_count = get_low_stock_report(cfg->low_stock_threshold, cfg->critical_stock_threshold,
                             cfg->max_stock_per_item, low_items, low_indices, &critical_count);

        if (low_count > 0)
        {
            LOG_INFO_MSG("Post-dispatch: %d items below threshold, requesting replenishment", low_count);

            message_t replenish_msg;
            inventory_item_t full_request_items[QUANTITY_ITEMS];

            if (build_full_request_payload(low_items, low_count, full_request_items) != 0)
            {
                LOG_ERROR_MSG("Failed to build full warehouse replenish payload");
                return -1;
            }

            if (create_items_message(&replenish_msg, WAREHOUSE_TO_SERVER__REPLENISH_REQUEST, shared_data->client_id,
                                     SERVER, full_request_items, QUANTITY_ITEMS, NULL) == 0)
            {
                if (enqueue_pending_message(&replenish_msg) == 0)
                {
                    mark_pending_stock_requests(low_indices, low_count);
                    LOG_INFO_MSG("Warehouse replenish request enqueued for %d items", low_count);
                }
                else
                {
                    LOG_ERROR_MSG("Failed to enqueue warehouse replenish request");
                }
            }
            else
            {
                LOG_ERROR_MSG("Failed to create warehouse replenish request");
            }
        }

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

        log_inventory_snapshot("RESTOCK recv");

        clear_pending_stock_requests(restock->items, QUANTITY_ITEMS);

        // Delay before sending receipt confirmation to avoid burst traffic
        random_response_delay();

        // Send receipt confirmation (role-specific message type)
        message_t receipt_msg;
        const char* receipt_type = (strcmp(shared_data->client_role, HUB) == 0)
                                       ? HUB_TO_SERVER__STOCK_RECEIPT_CONFIRMATION
                                       : WAREHOUSE_TO_SERVER__STOCK_RECEIPT_CONFIRMATION;

        if (create_items_message(&receipt_msg, receipt_type, shared_data->client_id, SERVER, restock->items,
                                 QUANTITY_ITEMS, restock->order_timestamp) != 0)
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
