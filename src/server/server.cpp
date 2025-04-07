#include "server/database.hpp"
#include <iostream>
#include <pqxx/pqxx>

int example_database()
{
    auto conn = connect_to_database();
    if (!conn)
    {
        std::cerr << "Failed to connect to the database." << std::endl;
        return 1;
    }
    try
    {
        pqxx::work txn(*conn);

        create_table(txn);

        insert_database(txn, 2, "Test Event", "Test Origin", "Test Level");
        txn.commit();

        conn->disconnect();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}