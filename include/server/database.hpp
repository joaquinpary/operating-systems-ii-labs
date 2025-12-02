#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <memory>
#include <pqxx/pqxx>
#include <string>

struct credential
{
    int id;
    std::string username;
    std::string password_hash;
    std::string client_type;
    bool is_active;
};

std::unique_ptr<pqxx::connection> connect_to_database();
std::unique_ptr<pqxx::connection> initialize_database();
int create_credentials_table(pqxx::work& txn);
std::unique_ptr<credential> query_credentials_by_username(pqxx::work& txn, const std::string& username);
int populate_credentials_table(pqxx::connection& conn, const std::string& json_file_path);

#endif
