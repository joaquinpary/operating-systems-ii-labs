#include "config.hpp"
#include "database.hpp"
#include "server.hpp"

#include <csignal>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        // Load configuration from file
        config::server_config cfg;
        config::load_config_from_file("config/server_config.json", cfg);

        // Initialize database (connect and create tables)
        auto db_connection = initialize_database();
        if (!db_connection)
        {
            throw std::runtime_error("Failed to initialize database. Server cannot start.");
        }

        // Convert to server internal config structure
        server_config server_cfg = make_server_config_from_config(cfg);

        asio::io_context io_context;
        server srv(io_context, server_cfg);

        asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](const asio::error_code&, int) {
            std::cout << "\nSignal received, stopping server...\n";
            srv.stop();
            io_context.stop();
        });

        srv.start();
        io_context.run();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
