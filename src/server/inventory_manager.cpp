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

    int quantities[QUANTITY_ITEMS] = {0};
    extract_quantities_from_payload(msg.payload.inventory_update, quantities);

    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());
        update_client_inventory(txn, msg.source_id, msg.source_role, quantities, msg.timestamp);
        txn.commit();
    }

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

    int quantities[QUANTITY_ITEMS] = {0};
    extract_quantities_from_payload(msg.payload.stock_request, quantities);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        result.items[i] = msg.payload.stock_request.items[i];
        if (msg.payload.stock_request.items[i].item_id != 0)
        {
            result.item_count++;
        }
    }

    try
    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());

        adjust_client_inventory(txn, std::string(msg.source_id), quantities, false);

        int transaction_id =
            create_transaction(txn, "STOCK_REQUEST", msg.source_id, msg.source_role, quantities, msg.timestamp);

        if (transaction_id < 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to create stock request transaction for hub " << msg.source_id
                      << std::endl;
            return result;
        }

        result.transaction_id = transaction_id;
        result.success = true;

        std::string warehouse_id = get_warehouse_with_all_stock(txn, quantities);

        if (!warehouse_id.empty())
        {
            set_transaction_source(txn, transaction_id, warehouse_id, "WAREHOUSE");
            mark_transaction_assigned(txn, transaction_id);

            result.warehouse_assigned = true;
            result.assigned_warehouse_id = warehouse_id;
        }

        txn.commit();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[INVENTORY_MANAGER] handle_stock_request error: " << ex.what() << std::endl;
        result.success = false;
    }

    return result;
}

stock_request_result inventory_manager::handle_replenish_request(const message_t& msg)
{
    stock_request_result result;
    result.transaction_id = -1;
    result.success = false;
    result.warehouse_assigned = false;
    result.item_count = 0;
    result.requesting_hub_id = "";

    int quantities[QUANTITY_ITEMS] = {0};
    extract_quantities_from_payload(msg.payload.restock_notice, quantities);

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        result.items[i] = msg.payload.restock_notice.items[i];
        if (msg.payload.restock_notice.items[i].item_id != 0)
        {
            result.item_count++;
        }
    }

    try
    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());

        int transaction_id =
            create_transaction(txn, "REPLENISH", msg.source_id, msg.source_role, quantities, msg.timestamp);

        if (transaction_id < 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to create transaction for warehouse " << msg.source_id
                      << std::endl;
            return result;
        }

        mark_transaction_dispatched(txn, transaction_id, msg.timestamp);

        txn.commit();

        result.transaction_id = transaction_id;
        result.success = true;
        result.warehouse_assigned = true;
        result.assigned_warehouse_id = std::string(msg.source_id);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[INVENTORY_MANAGER] handle_replenish_request error: " << ex.what() << std::endl;
    }

    return result;
}

int inventory_manager::handle_receipt_confirmation(const message_t& msg)
{
    try
    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());

        int transaction_id = ::find_transaction_id(txn, "", std::string(msg.source_id), "DISPATCHED");

        if (transaction_id < 0)
        {
            int dup_id = ::find_transaction_id(txn, "", std::string(msg.source_id), "COMPLETED");
            if (dup_id >= 0)
                return 0;
            return -1;
        }

        int quantities[QUANTITY_ITEMS] = {0};
        extract_quantities_from_payload(msg.payload.receipt_confirmation, quantities);

        if (adjust_client_inventory(txn, std::string(msg.source_id), quantities, true) != 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to add inventory for " << msg.source_id << std::endl;
            return -1;
        }

        complete_transaction(txn, transaction_id, msg.timestamp);

        txn.commit();
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[INVENTORY_MANAGER] handle_receipt_confirmation error: " << ex.what() << std::endl;
        return -1;
    }
}

stock_request_result inventory_manager::handle_shipment_notice(const message_t& msg)
{
    stock_request_result result;
    result.transaction_id = -1;
    result.success = false;
    result.warehouse_assigned = false;
    result.item_count = 0;

    try
    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());

        int transaction_id = ::find_transaction_id(txn, msg.source_id, "", "ASSIGNED");

        if (transaction_id < 0)
        {
            int dup_id = ::find_transaction_id(txn, msg.source_id, "", "DISPATCHED");
            if (dup_id < 0)
                dup_id = ::find_transaction_id(txn, msg.source_id, "", "COMPLETED");
            if (dup_id >= 0)
            {
                result.transaction_id = dup_id;
                result.success = true;
                return result;
            }
            return result;
        }

        int quantities[QUANTITY_ITEMS] = {0};
        extract_quantities_from_payload(msg.payload.shipment_notice, quantities);

        if (adjust_client_inventory(txn, std::string(msg.source_id), quantities, false) != 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to subtract inventory from warehouse " << msg.source_id
                      << std::endl;
            return result;
        }

        mark_transaction_dispatched(txn, transaction_id, msg.timestamp);

        transaction_record txn_record;
        if (get_transaction_by_id(txn, transaction_id, txn_record) != 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to get transaction " << transaction_id << " details" << std::endl;
            txn.commit();
            result.success = true;
            result.transaction_id = transaction_id;
            return result;
        }

        txn.commit();

        result.transaction_id = transaction_id;
        result.success = true;
        result.warehouse_assigned = true;
        result.assigned_warehouse_id = std::string(msg.source_id);
        result.requesting_hub_id = txn_record.destination_id;

        const char* item_names[QUANTITY_ITEMS] = {"food", "water", "medicine", "tools", "guns", "ammo"};
        result.item_count = 0;
        for (int i = 0; i < QUANTITY_ITEMS; i++)
        {
            if (quantities[i] > 0)
            {
                result.items[result.item_count].item_id = i + 1;
                strncpy(result.items[result.item_count].item_name, item_names[i], ITEM_NAME_SIZE - 1);
                result.items[result.item_count].quantity = quantities[i];
                result.item_count++;
            }
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[INVENTORY_MANAGER] handle_shipment_notice error: " << ex.what() << std::endl;
    }

    return result;
}

std::vector<stock_request_result> inventory_manager::process_pending_orders(const std::string& warehouse_id)
{
    std::vector<stock_request_result> fulfilled;

    try
    {
        auto guard = m_pool.acquire();
        pqxx::work txn(guard.get());

        int stock[QUANTITY_ITEMS] = {0};
        if (get_client_inventory(txn, warehouse_id, "WAREHOUSE", stock) != 0)
        {
            return fulfilled;
        }

        transaction_record pending[MAX_PENDING_TRANSACTIONS];
        int count = get_pending_transactions(txn, pending, MAX_PENDING_TRANSACTIONS);

        if (count == 0)
        {
            return fulfilled;
        }

        const char* item_names[QUANTITY_ITEMS] = {"food", "water", "medicine", "tools", "guns", "ammo"};

        for (int i = 0; i < count; i++)
        {
            int quantities[QUANTITY_ITEMS] = {pending[i].food,  pending[i].water, pending[i].medicine,
                                 pending[i].tools, pending[i].guns,  pending[i].ammo};

            bool can_fulfill = true;
            for (int j = 0; j < QUANTITY_ITEMS; j++)
            {
                if (quantities[j] > stock[j])
                {
                    can_fulfill = false;
                    break;
                }
            }

            if (can_fulfill)
            {
                set_transaction_source(txn, pending[i].transaction_id, warehouse_id, "WAREHOUSE");
                mark_transaction_assigned(txn, pending[i].transaction_id);

                stock_request_result result;
                result.transaction_id = pending[i].transaction_id;
                result.success = true;
                result.warehouse_assigned = true;
                result.assigned_warehouse_id = warehouse_id;
                result.requesting_hub_id = pending[i].destination_id;

                result.item_count = 0;
                for (int j = 0; j < QUANTITY_ITEMS; j++)
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

                break;
            }
        }

        txn.commit();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[INVENTORY_MANAGER] process_pending_orders error: " << ex.what() << std::endl;
    }

    return fulfilled;
}

void inventory_manager::extract_quantities_from_payload(const payload_items_list& payload, int quantities[6])
{
    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        quantities[i] = 0;
    }

    for (int i = 0; i < QUANTITY_ITEMS; i++)
    {
        int item_id = payload.items[i].item_id;
        if (item_id >= 1 && item_id <= QUANTITY_ITEMS)
        {
            quantities[item_id - 1] = payload.items[i].quantity;
        }
    }
}

int inventory_manager::find_transaction_id(const std::string& source_id, const std::string& destination_id,
                                           const std::string& status)
{
    auto guard = m_pool.acquire();
    return ::find_transaction_id(guard.get(), source_id, destination_id, status);
}

bool inventory_manager::get_client_inventory_message(const std::string& client_id, const std::string& client_type,
                                                     message_t& out_msg)
{
    int quantities[QUANTITY_ITEMS] = {0};
    {
        auto guard = m_pool.acquire();
        if (get_client_inventory(guard.get(), client_id, client_type, quantities) != 0)
        {
            std::cerr << "[INVENTORY_MANAGER] Failed to get inventory for client " << client_id << std::endl;
            return false;
        }
    }

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

    const char* item_names[QUANTITY_ITEMS] = {"food", "water", "medicine", "tools", "guns", "ammo"};
    inventory_item_t items[QUANTITY_ITEMS];
    memset(items, 0, sizeof(items));

    int item_count = 0;
    for (int i = 0; i < QUANTITY_ITEMS; i++)
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

    return true;
}
