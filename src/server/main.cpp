#include "config.hpp"
#include "server.hpp"

#include <csignal>
#include <iostream>

int main()
{
    try
    {
        // Load configuration from file
        config::server_config cfg;
        config::load_config_from_file("config/server_config.json", cfg);

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
