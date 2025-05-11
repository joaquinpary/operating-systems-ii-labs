#include "database.hpp"
#include "cJSON.h"
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
        printf("Intentando conectar a la db.\n");
        fflush(stdout);
        for (int i = 0; i < 10; ++i)
        {
            try
            {
                std::cout << "Intento nro: " << i + 1 << "\n" << std::flush;
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
            std::cout << "Conexión exitosa a PostgreSQL\n" << std::flush;
            initialize_database();
        }
        else
        {
            throw std::runtime_error("No se pudo abrir la conexión");
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error de inicialización de DB: " << e.what() << "\n";
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
             "client_type TEXT UNIQUE NOT NULL,"
             "username TEXT UNIQUE NOT NULL,"
             "password TEXT NOT NULL,"
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

        return !result.empty(); // Si el resultado no está vacío, hay una coincidencia
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "Error SQL en autenticación: " << e.what() << "\nConsulta: " << e.query() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error general en autenticación: " << e.what() << "\n";
    }

    return false;
}

void database_manager::load_clients_from_json(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Error opening the credentials file: " << filepath << "\n";
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_content = buffer.str();
    file.close();

    cJSON* root = cJSON_Parse(json_content.c_str());
    if (!root)
    {
        std::cerr << "Error parsing JSON: " << cJSON_GetErrorPtr() << "\n";
        return;
    }

    cJSON* clients = cJSON_GetObjectItemCaseSensitive(root, "clients");
    if (!cJSON_IsArray(clients))
    {
        std::cerr << "Clients is not a valid array.\n";
        cJSON_Delete(root);
        return;
    }

    pqxx::work txn(*database_conn_handle);
    for (cJSON* client = clients->child; client != nullptr; client = client->next)
    {
        cJSON* client_id = cJSON_GetObjectItemCaseSensitive(client, "client_id");
        cJSON* username = cJSON_GetObjectItemCaseSensitive(client, "username");
        cJSON* password = cJSON_GetObjectItemCaseSensitive(client, "password");

        if (!cJSON_IsString(client_id) || !cJSON_IsString(username) || !cJSON_IsString(password))
        {
            std::cerr << "Invalid format. Skipping credentials.\n";
            continue;
        }

        // Verificar si ya existe
        pqxx::result r =
            txn.exec(pqxx::zview("SELECT 1 FROM clients WHERE username = $1 OR client_id::text = $2 LIMIT 1"),
                     pqxx::params(username->valuestring, client_id->valuestring));
        if (!r.empty())
        {
            continue;
        }

        // Insertar nuevo cliente
        txn.exec(pqxx::zview("INSERT INTO clients (username, password, client_id) VALUES ($1, $2, $3)"),
                 pqxx::params(username->valuestring, password->valuestring, client_id->valuestring));
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
        std::cout << "Actualizando inventario de cliente: " << username << "\n" << std::flush;
        pqxx::result r =
            txn.exec(pqxx::zview("INSERT INTO inventory (id, water, food, medicine, guns, ammo, tools) "
                                 "VALUES ((SELECT id FROM clients WHERE username = $1), $2, $3, $4, $5, $6, $7) "
                                 "ON CONFLICT (id) DO UPDATE SET "
                                 "water = EXCLUDED.water, food = EXCLUDED.food, medicine = EXCLUDED.medicine, guns = "
                                 "EXCLUDED.guns, ammo = EXCLUDED.ammo, tools = EXCLUDED.tools "

                                 ),
                     pqxx::params(username, guns, ammo, medicine, tools, food, water));

        if (r.affected_rows() == 0)
        {
            std::cerr << "Client not found. Did not update inventory." << username << "\n" << std::flush;
            return true;
        }
        std::cout << "Inventario actualizado correctamente.\n";
        txn.commit();
        return false;
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "Error SQL en actualización de inventario: " << e.what() << "\nConsulta: " << e.query() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error general en actualización de inventario: " << e.what() << "\n";
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

        if (r.empty())
        {
            std::cerr << "Error al registrar la transacción.\n";
            return true;
        }

        txn.commit();
        return false;
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "Error SQL en registro de transacción: " << e.what() << "\nConsulta: " << e.query() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error general en registro de transacción: " << e.what() << "\n";
    }
    return true;
}

bool database_manager::register_warehouse_shipment(const std::string& warehouse_username,
                                                   const std::string& hub_username, int water, int food, int medicine,
                                                   int guns, int ammo, int tools,
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
                                              "AND timestamp_dispatched IS NULL "
                                              "RETURNING sender_id"),
                                  pqxx::params(timestamp_dispatched, warehouse_username, hub_username, water, food,
                                               medicine, guns, ammo, tools));

        if (r.empty())
        {
            std::cerr << "No se encontró una transacción coincidente para actualizar.\n";
            return false;
        }

        int warehouse_id = r[0][0].as<int>();

        // Actualizar el inventario del warehouse
        txn.exec(pqxx::zview("UPDATE inventory "
                             "SET water = water - $1, food = food - $2, medicine = medicine - $3, "
                             "guns = guns - $4, ammo = ammo - $5, tools = tools - $6 "
                             "WHERE client_id = $7"),
                 pqxx::params(water, food, medicine, guns, ammo, tools, warehouse_id));

        txn.commit();
        return true;
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "Error SQL en actualización de envío del warehouse: " << e.what() << "\nConsulta: " << e.query()
                  << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error general en actualización de envío del warehouse: " << e.what() << "\n";
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
            pqxx::params(9999, warehouse_username, water, food, medicine, guns, ammo, tools, timestamp));

        if (r.empty())
        {
            std::cerr << "Error al registrar la solicitud de stock del warehouse.\n";
            return false;
        }

        txn.commit();
        return true;
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "Error SQL en registro de solicitud de stock del warehouse: " << e.what()
                  << "\nConsulta: " << e.query() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error general en registro de solicitud de stock del warehouse: " << e.what() << "\n";
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
                                              "JOIN inventory i ON c.id = i.client_id "
                                              "WHERE c.role = 'warehouse' "
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
    }
    catch (const pqxx::sql_error& e)
    {
        std::cerr << "SQL error: " << e.what() << "\nQuery: " << e.query() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return {};
}
