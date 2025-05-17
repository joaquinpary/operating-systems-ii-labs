#include "database.hpp"
#include "cJSON.h"
#include "logger.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

database_manager::database_manager()
{
    try
    {
        // Obtener variables de entorno con valores por defecto
        const std::string db_name = get_env("POSTGRES_DB", "dhl_db");
        const std::string db_user = get_env("POSTGRES_USER", "dhl_user");
        const std::string db_pass = get_env("POSTGRES_PASSWORD", "dhl_pass");
        const std::string db_host = get_env("POSTGRES_HOST", "localhost"); // Nombre del servicio en docker-compose

        // Construir connection string de forma segura
        const std::string database_conn_handle_str =
            "dbname=" + db_name + " user=" + db_user + " password=" + db_pass + " host=" + db_host + " port=5432";

        // database_conn_handle = std::make_unique<pqxx::connection>(database_conn_handle_str);
        for (int i = 0; i < 10; ++i)
        {
            try
            {
                database_conn_handle = std::make_unique<pqxx::connection>(database_conn_handle_str);
                if (database_conn_handle->is_open())
                    break;
            }
            catch (...)
            {
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        if (database_conn_handle->is_open())
        {
            log_info("Successfully connected to PostgreSQL");
            initialize_database();
        }
        else
        {
            throw std::runtime_error("No se pudo abrir la conexión");
        }
    }
    catch (const std::exception& e)
    {
        log_error("Error de inicialización de DB: %s", e.what());
        throw;
    }
}

// void database_manager::log_message(const std::string& message) {
//     try {
//         pqxx::work txn(*database_conn_handle);
//         txn.exec_params("INSERT INTO server_logs (message) VALUES ($1)", std::string_view(message));
//         txn.commit();
//     } catch (const pqxx::sql_error& e) {
//         std::cerr << "Error SQL: " << e.what() << "\nConsulta: " << e.query() << "\n";
//     } catch (const std::exception& e) {
//         std::cerr << "Error general: " << e.what() << "\n";
//     }
// }

std::string database_manager::get_env(const char* name, const std::string& default_val)
{
    const char* val = std::getenv(name);
    return val ? std::string(val) : default_val;
}

void database_manager::initialize_database()
{
    pqxx::work txn(*database_conn_handle);

    txn.exec("CREATE TABLE IF NOT EXISTS clients ("
             "id SERIAL PRIMARY KEY,"
             "client_type TEXT NOT NULL,"
             "username TEXT UNIQUE NOT NULL,"
             "password TEXT NOT NULL,"
             "is_active BOOLEAN DEFAULT FALSE,"
             "session_token TEXT)");

    txn.exec("CREATE TABLE IF NOT EXISTS inventory ("
             "id INTEGER PRIMARY KEY REFERENCES clients(id),"
             "water INTEGER DEFAULT 0,"
             "food INTEGER DEFAULT 0,"
             "medicine INTEGER DEFAULT 0,"
             "guns INTEGER DEFAULT 0,"
             "ammo INTEGER DEFAULT 0,"
             "tools INTEGER DEFAULT 0,"
             "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");

    txn.exec("CREATE TABLE IF NOT EXISTS transaction_history ("
             "id SERIAL PRIMARY KEY,"
             "sender_id INTEGER REFERENCES clients(id), "
             "receiver_id INTEGER REFERENCES clients(id), "
             "timestamp_requested TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
             "timestamp_dispatched TIMESTAMP, "
             "timestamp_received TIMESTAMP, "
             "description TEXT, "
             "delta_water INTEGER DEFAULT 0, "
             "delta_food INTEGER DEFAULT 0, "
             "delta_medicine INTEGER DEFAULT 0, "
             "delta_guns INTEGER DEFAULT 0, "
             "delta_ammo INTEGER DEFAULT 0, "
             "delta_tools INTEGER DEFAULT 0, "
             "status TEXT CHECK (status IN ('requested', 'dispatched', 'received')) NOT NULL DEFAULT 'requested'"
             ");");

    txn.exec("CREATE TABLE IF NOT EXISTS logs ("
             "id SERIAL PRIMARY KEY REFERENCES clients(id),"
             "timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
             "actor TEXT NOT NULL,"
             "message TEXT NOT NULL)");

    txn.commit();

    load_clients_from_json(PATH_CREDENTIALS);
}

bool database_manager::authenticate_client(const std::string& username, const std::string& password)
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result result =
            txn.exec(pqxx::zview("SELECT 1 FROM clients WHERE username = $1 AND password = $2 LIMIT 1"),
                     pqxx::params{username, password});
        if (result.empty())
        {
            // log_error("Client authentication failed. Username: %s, Password: %s", username.c_str(), password.c_str());
            log_error("Client authentication failed");
            return false;
        }
        return true; // If the result is not empty, there is a match
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("SQL authentication error: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General authentication error: %s", e.what());
    }

    return false;
}

void database_manager::load_clients_from_json(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        log_error("Error opening the credentials file: %s", filepath.c_str());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();
    file.close();

    cJSON* root = cJSON_Parse(json_content.c_str());
    if (!root)
    {
        log_error("Error parsing JSON: %s", cJSON_GetErrorPtr());
        return;
    }

    cJSON* clients = cJSON_GetObjectItemCaseSensitive(root, "clients");
    if (!cJSON_IsArray(clients))
    {
        log_error("Clients is not a valid array");
        cJSON_Delete(root);
        return;
    }

    pqxx::work txn(*database_conn_handle);
    for (cJSON* client = clients->child; client != nullptr; client = client->next)
    {
        cJSON* client_type = cJSON_GetObjectItemCaseSensitive(client, "client_type");
        cJSON* username = cJSON_GetObjectItemCaseSensitive(client, "username");
        cJSON* password = cJSON_GetObjectItemCaseSensitive(client, "password");

        if (!cJSON_IsString(client_type) || !cJSON_IsString(username) || !cJSON_IsString(password))
        {
            log_error("Invalid format. Skipping credentials");
            continue;
        }

        // Verificar si ya existe
        pqxx::result r = txn.exec(pqxx::zview("SELECT 1 FROM clients WHERE username = $1 LIMIT 1"),
                                  pqxx::params(username->valuestring));
        if (!r.empty())
        {
            continue;
        }

        // Insertar nuevo cliente
        txn.exec(pqxx::zview("INSERT INTO clients (username, password, client_type) VALUES ($1, $2, $3)"),
                 pqxx::params(username->valuestring, password->valuestring, client_type->valuestring));
    }

    txn.commit();
    cJSON_Delete(root);
}

bool database_manager::update_client_inventory(const std::string& username, int water, int food, int medicine, int guns,
                                               int ammo, int tools)
{
    try
    {
        pqxx::work txn(*database_conn_handle);
        pqxx::result r =
            txn.exec(pqxx::zview("INSERT INTO inventory (id, water, food, medicine, guns, ammo, tools) "
                                 "VALUES ((SELECT id FROM clients WHERE username = $1), $2, $3, $4, $5, $6, $7) "
                                 "ON CONFLICT (id) DO UPDATE SET "
                                 "water = EXCLUDED.water, food = EXCLUDED.food, medicine = EXCLUDED.medicine, guns = "
                                 "EXCLUDED.guns, ammo = EXCLUDED.ammo, tools = EXCLUDED.tools "),
                     pqxx::params(username, water, food, medicine, guns, ammo, tools));

        if (r.affected_rows() == 0)
        {
            log_error("Client not found. Did not update inventory: %s", username.c_str());
            return true;
        }
        log_info("Inventory updated correctly");
        txn.commit();
        return false;
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("Error SQL in inventory update: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General error in inventory update: %s (Type: %s)", e.what(), typeid(e).name());
    }

    return true;
}

bool database_manager::register_hub_order(const std::string& warehouse_username, const std::string& hub_username,
                                          int water, int food, int medicine, int guns, int ammo, int tools,
                                          const std::string& timestamp)
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result r = txn.exec(
            pqxx::zview("INSERT INTO transaction_history (sender_id, receiver_id, "
                        "delta_water, delta_food, delta_medicine, delta_guns, delta_ammo, "
                        "delta_tools, timestamp_requested) "
                        "VALUES ((SELECT id FROM clients WHERE username = $1), "
                        "(SELECT id FROM clients WHERE username = $2), $3, $4, $5, $6, $7, $8, $9) "),
            pqxx::params(hub_username, warehouse_username, water, food, medicine, guns, ammo, tools, timestamp));

        if (r.affected_rows() == 0)
        {
            log_error("Error registering hub order");
            return true;
        }

        txn.commit();
        return false;
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("Error SQL in hub order registration: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General error in hub order registration: %s", e.what());
    }
    return true;
}

bool database_manager::register_warehouse_shipment(const std::string& sender_username,
                                                   const std::string& receiver_username, int water, int food,
                                                   int medicine, int guns, int ammo, int tools,
                                                   const std::string& timestamp_dispatched)
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result r = txn.exec(pqxx::zview("UPDATE transaction_history "
                                              "SET timestamp_dispatched = $1 "
                                              "WHERE sender_id = (SELECT id FROM clients WHERE username = $2) "
                                              "AND receiver_id = (SELECT id FROM clients WHERE username = $3) "
                                              "AND delta_water = $4 AND delta_food = $5 AND delta_medicine = $6 "
                                              "AND delta_guns = $7 AND delta_ammo = $8 AND delta_tools = $9 "
                                              "AND timestamp_dispatched IS NULL "),
                                  pqxx::params(timestamp_dispatched, sender_username, receiver_username, water, food,
                                               medicine, guns, ammo, tools));

        if (r.affected_rows() == 0)
        {
            log_error("Did not find a matching transaction to update");
            return false;
        }
        log_info("Warehouse shipment updated correctly");
        txn.commit();
        return true;
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("Error SQL in warehouse shipment update: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General error in warehouse shipment update: %s (Type: %s)", e.what(), typeid(e).name());
    }
    return false;
}

bool database_manager::register_warehouse_stock_request(const std::string& warehouse_username, int water, int food,
                                                        int medicine, int guns, int ammo, int tools,
                                                        const std::string& timestamp)
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result r = txn.exec(
            pqxx::zview(
                "INSERT INTO transaction_history (sender_id, receiver_id, "
                "delta_water, delta_food, delta_medicine, delta_guns, delta_ammo, delta_tools, timestamp_requested) "
                "VALUES ((SELECT id FROM clients WHERE username = $1), "
                "(SELECT id FROM clients WHERE username = $2), $3, $4, $5, $6, $7, $8, $9)"),
            pqxx::params(warehouse_username, warehouse_username, water, food, medicine, guns, ammo, tools, timestamp));

        if (r.affected_rows() == 0)
        {
            log_error("Error registering warehouse stock request");
            return false;
        }

        txn.commit();
        return true;
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("Error SQL in warehouse stock request registration: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General error in warehouse stock request registration: %s", e.what());
    }
    return false;
}

std::string database_manager::get_designated_warehouse(int water, int food, int medicine, int guns, int ammo, int tools)
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result r = txn.exec(pqxx::zview("SELECT c.username "
                                              "FROM clients c "
                                              "JOIN inventory i ON c.id = i.id "
                                              "WHERE c.client_type = 'warehouse' "
                                              "AND c.is_active = TRUE "
                                              "AND i.water >= $1 "
                                              "AND i.food >= $2 "
                                              "AND i.medicine >= $3 "
                                              "AND i.guns >= $4 "
                                              "AND i.ammo >= $5 "
                                              "AND i.tools >= $6 "
                                              "ORDER BY RANDOM() "
                                              "LIMIT 1"),
                                  pqxx::params(water, food, medicine, guns, ammo, tools));

        txn.commit();

        if (!r.empty())
        {
            return r[0][0].as<std::string>();
        }
        else
        {
            log_error("No warehouse found to fulfill the request");
            return {};
        }
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("SQL error in warehouse selection: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("Error in warehouse selection: %s", e.what());
    }

    return {};
}

bool database_manager::retrieve_client_inventory(const std::string& username, inventory_item items[ITEM_TYPE])
{
    try
    {
        pqxx::work txn(*database_conn_handle);

        pqxx::result r = txn.exec(pqxx::zview("SELECT i.water, i.food, i.medicine, i.guns, i.ammo, i.tools "
                                              "FROM inventory i "
                                              "JOIN clients c ON c.id = i.id "
                                              "WHERE c.username = $1"),
                                  pqxx::params(username));

        if (r.empty())
        {
            log_info("No inventory found for client: %s. Initializing with 100 units.", username.c_str());
            // Inicializar con 100 unidades de cada item
            for (int i = 0; i < ITEM_TYPE; i++)
            {
                items[i].quantity = 100;
            }
            return true;
        }

        items[0].quantity = r[0][0].as<int>();
        items[1].quantity = r[0][1].as<int>();
        items[2].quantity = r[0][2].as<int>();
        items[3].quantity = r[0][3].as<int>();
        items[4].quantity = r[0][4].as<int>();
        items[5].quantity = r[0][5].as<int>();

        txn.commit();
        return true;
    }
    catch (const pqxx::sql_error& e)
    {
        log_error("Error SQL in inventory retrieval: %s\nQuery: %s", e.what(), e.query().c_str());
    }
    catch (const std::exception& e)
    {
        log_error("General error in inventory retrieval: %s", e.what());
    }
    return false;
}

bool database_manager::register_hub_stock_confirm(const std::string& sender_username,
                                                  const std::string& receiver_username, int water, int food,
                                                  int medicine, int guns, int ammo, int tools,
                                                  const std::string& timestamp)
{
    try
    {
        pqxx::work txn(*database_conn_handle);
        // Actualizar el inventario del hub
        pqxx::result r = txn.exec(
            pqxx::zview("UPDATE transaction_history "
                        "SET timestamp_received = $1 "
                        "WHERE sender_id = (SELECT id FROM clients WHERE username = $2) "
                        "AND receiver_id = (SELECT id FROM clients WHERE username = $3) "
                        "AND delta_water = $4 AND delta_food = $5 AND delta_medicine = $6 "
                        "AND delta_guns = $7 AND delta_ammo = $8 AND delta_tools = $9 "
                        "AND timestamp_received IS NULL "),
            pqxx::params(timestamp, sender_username, receiver_username, water, food, medicine, guns, ammo, tools));

        if (r.affected_rows() == 0)
        {
            log_error("Did not find a matching transaction to update");
            return false;
        }

        txn.commit();
        log_info("Hub stock confirmed");

        return true;
    }
    catch (const std::exception& e)
    {
        log_error("Error in hub stock confirmation: %s", e.what());
        return false;
    }
}

bool database_manager::set_client_active(const std::string& username)
{
    pqxx::work txn(*database_conn_handle);
    txn.exec(pqxx::zview("UPDATE clients SET is_active = TRUE WHERE username = $1"), pqxx::params{username});
    txn.commit();
    return true;
}

bool database_manager::set_client_inactive(const std::string& username)
{
    pqxx::work txn(*database_conn_handle);
    txn.exec(pqxx::zview("UPDATE clients SET is_active = FALSE WHERE username = $1"), pqxx::params{username});
    txn.commit();
    return true;
}
