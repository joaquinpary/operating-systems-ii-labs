#include <iostream>
#include <pqxx/pqxx>

// host = db for docker to docker
// host = localhost for local to docker

std::unique_ptr<pqxx::connection> connect_to_database()
{
    try
    {
        auto conn = std::make_unique<pqxx::connection>(
            "dbname=dhl_db user=dhl_user password=dhl_pass host=localhost port=5432");
        // auto conn = std::make_unique<pqxx::connection>("dbname=dhl_db user=dhl_user password=dhl_pass host=db
        // port=5432");
        if (conn->is_open())
        {
            std::cout << "Successful connection to PostgreSQL: " << conn->dbname() << std::endl;
            return conn;
        }
        else
        {
            std::cerr << "Connection Error." << std::endl;
            conn->disconnect();
            return nullptr;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return nullptr;
    }
}

int create_table(pqxx::work& txn)
{
    try
    {
        std::string sql = "CREATE TABLE IF NOT EXISTS " + txn.quote_name("logs") + " (" + txn.quote_name("id") +
                          " SERIAL PRIMARY KEY, " + txn.quote_name("packet_id") + " INTEGER, " +
                          txn.quote_name("event") + " TEXT NOT NULL, " + txn.quote_name("timestamp") +
                          " TIMESTAMP DEFAULT CURRENT_TIMESTAMP, " + txn.quote_name("origin") + " TEXT, " +
                          txn.quote_name("level") + " TEXT);";

        txn.exec(sql);
        std::cout << "Table created successfully." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error creating table: " << e.what() << std::endl;
        throw;
        return 1;
    }
}

int insert_database(pqxx::work& txn, const int& packet_id, const std::string& event, const std::string& origin,
                   const std::string& level)
{
    try
    {
        txn.exec_params("INSERT INTO logs (packet_id, event, origin, level) VALUES ($1, $2, $3, $4)", packet_id, event,
                        origin, level);

        txn.commit();
        std::cout << "Log inserted successfully." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error inserting package: " << e.what() << std::endl;
        throw;
        return 1;
    }
}
