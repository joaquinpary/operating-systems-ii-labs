#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <memory>
#include <pqxx/pqxx>

class database_manager {
public:
    database_manager();
    void log_message(const std::string& message);
    bool authenticate_client(const std::string& username, const std::string& password);

private:
    std::string get_env(const char* name, const std::string& default_val);
    void initialize_database();
    void load_clients_from_json(const std::string& filepath);

    std::unique_ptr<pqxx::connection> database_conn_handle;
};

#endif // DATABASE_HPP
