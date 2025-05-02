#include "database.hpp"
#include "cJSON.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

#define PATH_CREDENTIALS "config/server_credentials.json"

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
             "client_id TEXT UNIQUE NOT NULL,"
             "username TEXT UNIQUE NOT NULL,"
             "password TEXT NOT NULL,"
             "session_token TEXT)");

    txn.exec("CREATE TABLE IF NOT EXISTS inventory ("
             "id INTEGER PRIMARY KEY REFERENCES clients(id),"
             "weapons INTEGER DEFAULT 0,"
             "medicine INTEGER DEFAULT 0,"
             "tools INTEGER DEFAULT 0,"
             "food INTEGER DEFAULT 0,"
             "water INTEGER DEFAULT 0)");

    txn.exec("CREATE TABLE IF NOT EXISTS transaction_history ("
             "id SERIAL PRIMARY KEY REFERENCES clients(id), "
             "hub_id TEXT REFERENCES clients(client_id), "
             "warehouse_id TEXT REFERENCES clients(client_id), "
             "timestamp_requested TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
             "timestamp_dispatched TIMESTAMP, "
             "timestamp_received TIMESTAMP, "
             "description TEXT, "
             "delta_weapons INTEGER DEFAULT 0, "
             "delta_medicine INTEGER DEFAULT 0, "
             "delta_tools INTEGER DEFAULT 0, "
             "delta_food INTEGER DEFAULT 0, "
             "delta_water INTEGER DEFAULT 0, "
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

        pqxx::result result = txn.exec_params("SELECT 1 FROM clients WHERE username = $1 AND password = $2 LIMIT 1",
                                              std::string_view(username), std::string_view(password));

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
            txn.exec_params("SELECT 1 FROM clients WHERE username = $1 OR client_id::text = $2 LIMIT 1",
                            std::string_view(username->valuestring), std::string_view(client_id->valuestring));
        if (!r.empty())
        {
            continue;
        }

        // Insertar nuevo cliente
        txn.exec_params("INSERT INTO clients (username, password, client_id) VALUES ($1, $2, $3)",
                        std::string_view(username->valuestring), std::string_view(password->valuestring),
                        std::string_view(client_id->valuestring));
    }

    txn.commit();
    cJSON_Delete(root);
}
