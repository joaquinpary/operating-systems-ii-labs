#include "auth_module.hpp"
#include "config.hpp"
#include "database.hpp"
#include "message_handler.hpp"
#include "server.hpp"
#include "session_manager.hpp"

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

        // Create core modules
        auto session_mgr = std::make_unique<session_manager>();
        auto auth_mod = std::make_unique<auth_module>(*db_connection);
        auto msg_handler = std::make_unique<message_handler>(*auth_mod, *session_mgr);

        asio::io_context io_context;
        server srv(io_context, cfg, std::move(session_mgr), std::move(auth_mod), std::move(msg_handler));

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
