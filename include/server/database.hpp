#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <memory>
#include <pqxx/pqxx>

std::unique_ptr<pqxx::connection> connectToDatabase();
int createTable(pqxx::work& txn);
int insertDatabase(pqxx::work& txn, const int& packet_id, const std::string& event, const std::string& origin,
                   const std::string& level);

#endif
