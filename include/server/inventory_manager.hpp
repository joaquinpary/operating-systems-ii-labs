#ifndef INVENTORY_MANAGER_HPP
#define INVENTORY_MANAGER_HPP

#include <common/json_manager.h>
#include <string>
#include <vector>

/** Maximum number of pending transactions loaded in one processing batch. */
#define MAX_PENDING_TRANSACTIONS 100

class connection_pool;

/** Result produced by inventory workflows and consumed by message_handler. */
struct stock_request_result
{
    int transaction_id;                     ///< Transaction identifier, or -1 on error.
    bool success;                           ///< Whether the workflow completed successfully.
    bool warehouse_assigned;                ///< Whether a warehouse was selected.
    std::string assigned_warehouse_id;      ///< Warehouse username/client_id selected for fulfillment.
    inventory_item_t items[QUANTITY_ITEMS]; ///< Copy of the items to include in outbound messages.
    int item_count;                         ///< Number of valid items stored in the array.
    std::string requesting_hub_id;          ///< Hub destination when the workflow originated there.
};

/** Coordinates inventory persistence and transaction state transitions. */
class inventory_manager
{
  public:
    /** Build an inventory manager backed by the shared database connection pool. */
    inventory_manager(connection_pool& pool);
    ~inventory_manager();

    /**
     * Persist a client inventory update and try to fulfill pending orders.
     * @param msg INVENTORY_UPDATE message received from a client.
     * @return Orders that became fulfillable after applying the update.
     */
    std::vector<stock_request_result> handle_inventory_update(const message_t& msg);

    /**
     * Create and resolve a stock request originated by a hub.
     * @param msg STOCK_REQUEST message from a hub.
     * @return Structured workflow result for downstream response generation.
     */
    stock_request_result handle_stock_request(const message_t& msg);

    /**
     * Register a replenish request sent by a warehouse.
     * @param msg REPLENISH_REQUEST message from a warehouse.
     * @return Structured workflow result used to build the RESTOCK_NOTICE response.
     */
    stock_request_result handle_replenish_request(const message_t& msg);

    /**
     * Complete the transaction referenced by a stock receipt confirmation.
     * @param msg STOCK_RECEIPT_CONFIRMATION message from a client.
     * @return 0 on success, negative value on error.
     */
    int handle_receipt_confirmation(const message_t& msg);

    /**
     * Process a dispatch confirmation from a hub (ORDER_DISPATCH flow).
     * Reduces the hub's inventory in the DB and marks the transaction DISPATCHED.
     * @param msg DISPATCH_CONFIRMATION message from a hub.
     * @return 0 on success, negative value on error.
     */
    int handle_dispatch_confirmation(const message_t& msg, int& out_transaction_id);

    /**
     * Update a transaction after a warehouse reports a shipment dispatch.
     * @param msg SHIPMENT_NOTICE message from a warehouse.
     * @return Workflow result with the destination hub to notify.
     */
    stock_request_result handle_shipment_notice(const message_t& msg);

    /**
     * Re-evaluate pending hub orders against one warehouse inventory snapshot.
     * @param warehouse_id Warehouse to test against pending orders.
     * @return Orders that can now be dispatched.
     */
    std::vector<stock_request_result> process_pending_orders(const std::string& warehouse_id);

    /**
     * Re-evaluate pending gateway orders (ORDER_DISPATCH) against one hub inventory snapshot.
     * Called when a HUB receives stock, to fulfill pending gateway dispatch requests.
     * @param hub_id HUB to test against pending gateway orders.
     * @return Orders that can now be dispatched by this HUB.
     */
    std::vector<stock_request_result> process_pending_gateway_orders(const std::string& hub_id);

    /**
     * Build an INVENTORY_UPDATE message from the stored inventory of a client.
     * @param client_id Client username/client_id.
     * @param client_type HUB or WAREHOUSE.
     * @param out_msg Output message populated on success.
     * @return true on success, false on error.
     */
    bool get_client_inventory_message(const std::string& client_id, const std::string& client_type, message_t& out_msg);

  private:
    connection_pool& m_pool;

    /** Extract item quantities from the JSON payload structure into a fixed array. */
    void extract_quantities_from_payload(const payload_items_list& payload, int quantities[6]);

    /** Find a transaction by source, destination and status. */
    int find_transaction_id(const std::string& source_id, const std::string& destination_id,
                            const std::string& status = "ASSIGNED");
};

#endif // INVENTORY_MANAGER_HPP
