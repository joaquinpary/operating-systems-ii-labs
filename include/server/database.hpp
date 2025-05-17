#ifndef DATABASE_HPP
#define DATABASE_HPP

#include "json_manager.h"
#include <memory>
#include <pqxx/pqxx>
#include <string>

#ifdef TESTING
#define PATH_CREDENTIALS "config/server_credentials.json"
#else
#define PATH_CREDENTIALS "/etc/dhl_server/server_credentials.json"
#endif

#define ITEM_TYPE 6

class database_manager
{
  public:
    database_manager();
    void log_message(const std::string& message);
    bool authenticate_client(const std::string& username, const std::string& password);
    bool update_client_inventory(const std::string& client_id, int water, int food, int medicine, int guns, int ammo,
                                 int tools);
    bool register_hub_order(const std::string& warehouse_username, const std::string& hub_username, int water, int food,
                            int medicine, int guns, int ammo, int tools, const std::string& timestamp);
    bool register_warehouse_shipment(const std::string& sender_username, const std::string& receiver_username,
                                     int water, int food, int medicine, int guns, int ammo, int tools,
                                     const std::string& timestamp);
    std::string get_designated_warehouse(int water, int food, int medicine, int guns, int ammo, int tools);
    bool register_warehouse_stock_request(const std::string& warehouse_username, int water, int food, int medicine,
                                          int guns, int ammo, int tools, const std::string& timcestamp);
    bool retrieve_client_inventory(const std::string& username, inventory_item items[ITEM_TYPE]);
    bool register_hub_stock_confirm(const std::string& sender_username, const std::string& receiver_username, int water,
                                    int food, int medicine, int guns, int ammo, int tools,
                                    const std::string& timestamp);
    bool set_client_active(const std::string& username);
    bool set_client_inactive(const std::string& username);

  private:
    std::string get_env(const char* name, const std::string& default_val);
    void initialize_database();
    void load_clients_from_json(const std::string& filepath);

    std::unique_ptr<pqxx::connection> database_conn_handle;
};

#endif // DATABASE_HPP
