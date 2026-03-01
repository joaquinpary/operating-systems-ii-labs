#include "inventory_manager.hpp"
#include "connection_pool.hpp"
#include "database.hpp"

#include <cstring>
#include <iostream>

inventory_manager::inventory_manager(connection_pool& pool) : m_pool(pool)
{
}

inventory_manager::~inventory_manager()
{
}

std::vector<stock_request_result> inventory_manager::handle_inventory_update(const message_t& msg)
{
    std::vector<stock_request_result> fulfilled_orders;

    std::cout << "[INVENTORY_MANAGER] Handling inventory update from " << msg.source_id << std::endl;

    // Extract quantities from payload
    int quantities[6] = {0};
    extract_quantities_from_payload(msg.payload.inventory_update, quantities);

    // Acquire a connection from the pool for this operation
    {
        auto guard = m_pool.acquire();

        // Update client inventory in database
        int result = update_client_inventory(guard.get(), msg.source_id, msg.source_role, quantities, msg.timestamp);

        if (result != 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to update inventory for " << msg.source_id << std::endl;
            return fulfilled_orders;
        }
    } // Release connection before calling process_pending_orders (which acquires its own)

    // If warehouse updated inventory, try to fulfill ONE pending order
    if (strcmp(msg.source_role, WAREHOUSE) == 0)
    {
        auto fulfilled = process_pending_orders(msg.source_id);
        if (!fulfilled.empty())
        {
            fulfilled_orders = std::move(fulfilled);
        }
    }

    return fulfilled_orders;
}

stock_request_result inventory_manager::handle_stock_request(const message_t& msg)
{
    stock_request_result result;
    result.transaction_id = -1;
    result.success = false;
    result.warehouse_assigned = false;
    result.item_count = 0;
    result.requesting_hub_id = std::string(msg.source_id);

    std::cout << "[INVENTORY_MANAGER] Handling stock request from hub " << msg.source_id << std::endl;

    // Extract quantities from payload
    int quantities[6] = {0};
    extract_quantities_from_payload(msg.payload.stock_request, quantities);

    // Copy items into result (for building dispatch messages later)
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        result.items[i] = msg.payload.stock_request.items[i];
        if (msg.payload.stock_request.items[i].item_id != 0)
        {
            result.item_count++;
        }
    }

    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    // Create transaction with hub as destination (stock will be sent TO the hub)
    int transaction_id =
        create_transaction(guard.get(), "STOCK_REQUEST", msg.source_id, msg.source_role, quantities, msg.timestamp);

    if (transaction_id < 0)
    {
        std::cerr << "[INVENTORY_MANAGER] Failed to create stock request transaction for hub " << msg.source_id
                  << std::endl;
        return result;
    }

    result.transaction_id = transaction_id;
    result.success = true;

    // Try to find a warehouse with sufficient stock
    std::string warehouse_id = get_warehouse_with_all_stock(guard.get(), quantities);

    if (warehouse_id.empty())
    {
        std::cout << "[INVENTORY_MANAGER] No warehouse available, order " << transaction_id << " queued as PENDING"
                  << std::endl;
        return result;
    }

    // Warehouse found — assign it as the source and mark as ASSIGNED (not dispatched yet)
    set_transaction_source(guard.get(), transaction_id, warehouse_id, "WAREHOUSE");
    mark_transaction_assigned(guard.get(), transaction_id);

    result.warehouse_assigned = true;
    result.assigned_warehouse_id = warehouse_id;

    std::cout << "[INVENTORY_MANAGER] Assigned order " << transaction_id << " to warehouse " << warehouse_id
              << " for hub " << msg.source_id << std::endl;
    return result;
}

stock_request_result inventory_manager::handle_replenish_request(const message_t& msg)
{
    stock_request_result result;
    result.transaction_id = -1;
    result.success = false;
    result.warehouse_assigned = false;
    result.item_count = 0;
    result.requesting_hub_id = ""; // empty = warehouse self-request

    std::cout << "[INVENTORY_MANAGER] Handling replenish request from warehouse " << msg.source_id << std::endl;

    // Extract quantities from payload
    int quantities[6] = {0};
    extract_quantities_from_payload(msg.payload.restock_notice, quantities);

    // Copy items into result (for building the RESTOCK_NOTICE response)
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        result.items[i] = msg.payload.restock_notice.items[i];
        if (msg.payload.restock_notice.items[i].item_id != 0)
        {
            result.item_count++;
        }
    }

    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    // Create transaction
    int transaction_id =
        create_transaction(guard.get(), "REPLENISH", msg.source_id, msg.source_role, quantities, msg.timestamp);

    if (transaction_id < 0)
    {
        std::cerr << "[INVENTORY_MANAGER] Failed to create transaction for warehouse " << msg.source_id << std::endl;
        return result;
    }

    // Immediately mark as ASSIGNED (warehouse will confirm with a dispatch/restock notice)
    mark_transaction_assigned(guard.get(), transaction_id);

    result.transaction_id = transaction_id;
    result.success = true;
    result.warehouse_assigned = true;
    result.assigned_warehouse_id = std::string(msg.source_id);

    std::cout << "[INVENTORY_MANAGER] Authorized restock for warehouse " << msg.source_id << ", transaction "
              << transaction_id << std::endl;

    return result;
}

int inventory_manager::handle_receipt_confirmation(const message_t& msg)
{
    // TODO: Implement
    std::cout << "[INVENTORY_MANAGER] Handling receipt confirmation from " << msg.source_id << std::endl;

    // Find transaction by source/destination
    // This needs more context - we need to know which transaction this confirmation is for
    // For now, stub

    return 0;
}

int inventory_manager::handle_shipment_notice(const message_t& msg)
{
    std::cout << "[INVENTORY_MANAGER] Handling shipment notice from warehouse " << msg.source_id << std::endl;

    // Find the ASSIGNED transaction for this warehouse
    // For now, find by source_id matching the warehouse
    int transaction_id = find_transaction_id(msg.source_id, "");

    if (transaction_id < 0)
    {
        std::cerr << "[INVENTORY_MANAGER] No assigned transaction found for warehouse " << msg.source_id << std::endl;
        return -1;
    }

    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    // Mark as DISPATCHED with the warehouse's timestamp
    mark_transaction_dispatched(guard.get(), transaction_id, msg.timestamp);

    std::cout << "[INVENTORY_MANAGER] Marked transaction " << transaction_id << " as DISPATCHED (from warehouse "
              << msg.source_id << ")" << std::endl;

    return 0;
}

std::vector<stock_request_result> inventory_manager::process_pending_orders(const std::string& warehouse_id)
{
    std::vector<stock_request_result> fulfilled;

    std::cout << "[INVENTORY_MANAGER] Processing pending orders after warehouse " << warehouse_id << " inventory update"
              << std::endl;

    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    // Get pending transactions
    transaction_record pending[MAX_PENDING_TRANSACTIONS];
    int count = get_pending_transactions(guard.get(), pending, MAX_PENDING_TRANSACTIONS);

    if (count == 0)
    {
        return fulfilled;
    }

    std::cout << "[INVENTORY_MANAGER] Found " << count << " pending orders, checking first fulfillable" << std::endl;

    // Item names corresponding to item_ids 1-6
    const char* item_names[6] = {"food", "water", "medicine", "tools", "guns", "ammo"};

    // Find the FIRST pending order that can be fulfilled — only one at a time
    for (int i = 0; i < count; i++)
    {
        int quantities[6] = {pending[i].food,  pending[i].water, pending[i].medicine,
                             pending[i].tools, pending[i].guns,  pending[i].ammo};

        std::string available_warehouse = get_warehouse_with_all_stock(guard.get(), quantities);

        if (!available_warehouse.empty())
        {
            // Assign warehouse as the SOURCE and mark as ASSIGNED
            set_transaction_source(guard.get(), pending[i].transaction_id, available_warehouse, "WAREHOUSE");
            mark_transaction_assigned(guard.get(), pending[i].transaction_id);

            // Build stock_request_result for message_handler to send dispatch
            stock_request_result result;
            result.transaction_id = pending[i].transaction_id;
            result.success = true;
            result.warehouse_assigned = true;
            result.assigned_warehouse_id = available_warehouse;
            result.requesting_hub_id = pending[i].destination_id;

            // Build inventory_item_t array from quantities
            result.item_count = 0;
            for (int j = 0; j < 6; j++)
            {
                if (quantities[j] > 0)
                {
                    result.items[result.item_count].item_id = j + 1;
                    strncpy(result.items[result.item_count].item_name, item_names[j],
                            sizeof(result.items[result.item_count].item_name) - 1);
                    result.items[result.item_count].quantity = quantities[j];
                    result.item_count++;
                }
            }

            fulfilled.push_back(result);

            std::cout << "[INVENTORY_MANAGER] Fulfilled pending order " << pending[i].transaction_id
                      << " with warehouse " << available_warehouse << " for hub " << pending[i].destination_id
                      << std::endl;

            // Only fulfill ONE order per inventory update
            break;
        }
    }

    return fulfilled;
}

void inventory_manager::extract_quantities_from_payload(const payload_items_list& payload, int quantities[6])
{
    // Initialize to zero
    for (int i = 0; i < 6; i++)
    {
        quantities[i] = 0;
    }

    // Extract from payload items array
    // Items have item_id (1-6) mapping to array index (0-5)
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        int item_id = payload.items[i].item_id;
        if (item_id >= 1 && item_id <= 6)
        {
            quantities[item_id - 1] = payload.items[i].quantity;
        }
    }
}

int inventory_manager::find_transaction_id(const std::string& source_id, const std::string& destination_id)
{
    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    // Implementation uses DB to find the latest ASSIGNED transaction for this source/destination
    return ::find_transaction_id(guard.get(), source_id, destination_id, "ASSIGNED");
}

bool inventory_manager::get_client_inventory_message(const std::string& client_id, const std::string& client_type,
                                                     message_t& out_msg)
{
    // Acquire a connection from the pool
    auto guard = m_pool.acquire();

    int quantities[6] = {0};
    if (get_client_inventory(guard.get(), client_id, quantities) != 0)
    {
        std::cerr << "[INVENTORY_MANAGER] Failed to get inventory for client " << client_id << std::endl;
        return false;
    }

    // Determine message type based on client type
    const char* msg_type = nullptr;
    if (client_type == HUB)
    {
        msg_type = SERVER_TO_HUB__INVENTORY_UPDATE;
    }
    else if (client_type == WAREHOUSE)
    {
        msg_type = SERVER_TO_WAREHOUSE__INVENTORY_UPDATE;
    }
    else
    {
        std::cerr << "[INVENTORY_MANAGER] Unknown client_type: " << client_type << std::endl;
        return false;
    }

    // Build inventory items from quantities
    const char* item_names[6] = {"food", "water", "medicine", "tools", "guns", "ammo"};
    inventory_item_t items[QUANTITY_ITEMS];
    memset(items, 0, sizeof(items));

    int item_count = 0;
    for (int i = 0; i < 6; i++)
    {
        items[item_count].item_id = i + 1;
        strncpy(items[item_count].item_name, item_names[i], ITEM_NAME_SIZE - 1);
        items[item_count].quantity = quantities[i];
        item_count++;
    }

    memset(&out_msg, 0, sizeof(message_t));
    if (create_items_message(&out_msg, msg_type, SERVER, client_id.c_str(), items, item_count, nullptr) != 0)
    {
        std::cerr << "[INVENTORY_MANAGER] Failed to create inventory message for " << client_id << std::endl;
        return false;
    }

    std::cout << "[INVENTORY_MANAGER] Built inventory message for client " << client_id << std::endl;
    return true;
}
