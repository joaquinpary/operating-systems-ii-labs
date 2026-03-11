#ifndef INVENTORY_MANAGER_HPP
#define INVENTORY_MANAGER_HPP

#include <common/json_manager.h>
#include <string>
#include <vector>

#define MAX_PENDING_TRANSACTIONS 100

// Forward declaration
class connection_pool;

// Result of handle_stock_request — tells message_handler what happened and what to send
struct stock_request_result
{
    int transaction_id; // -1 on error
    bool success;

    // If a warehouse was assigned, these are populated:
    bool warehouse_assigned;
    std::string assigned_warehouse_id; // warehouse username/client_id

    // Copy of the requested items (for building the dispatch message)
    inventory_item_t items[QUANTITY_ITEMS];
    int item_count;

    // The hub that requested the stock (destination)
    std::string requesting_hub_id;
};

class inventory_manager
{
  public:
    inventory_manager(connection_pool& pool);
    ~inventory_manager();

    // Handle inventory update from hub or warehouse
    // Returns fulfilled pending orders (if any were resolved by a warehouse inventory update)
    std::vector<stock_request_result> handle_inventory_update(const message_t& msg);

    // Handle stock request from hub
    // Returns structured result for message_handler to act on
    stock_request_result handle_stock_request(const message_t& msg);

    // Handle replenish request from warehouse
    // Returns structured result for message_handler to send RESTOCK_NOTICE
    stock_request_result handle_replenish_request(const message_t& msg);

    // Handle receipt confirmation from hub or warehouse
    int handle_receipt_confirmation(const message_t& msg);

    // Handle shipment notice from warehouse
    // Returns stock_request_result with hub destination info so message_handler can notify the hub
    stock_request_result handle_shipment_notice(const message_t& msg);

    // Process pending orders when a warehouse updates its inventory
    // Returns fulfilled orders so message_handler can send dispatch messages
    std::vector<stock_request_result> process_pending_orders(const std::string& warehouse_id);

    // Build an INVENTORY_UPDATE message from the client's stored inventory in the database
    // Returns true on success (out_msg populated), false on error
    bool get_client_inventory_message(const std::string& client_id, const std::string& client_type, message_t& out_msg);

  private:
    connection_pool& m_pool;

    // Helper: Extract quantities from message payload
    void extract_quantities_from_payload(const payload_items_list& payload, int quantities[6]);

    // Helper: Find transaction by source, destination, and status
    int find_transaction_id(const std::string& source_id, const std::string& destination_id,
                            const std::string& status = "ASSIGNED");
};

#endif // INVENTORY_MANAGER_HPP
