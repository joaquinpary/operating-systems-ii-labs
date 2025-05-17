#include "config.hpp"
#include "database.hpp"
#include "dhl_server.hpp"
#include "logger.h"
#include <asio.hpp>
#include <atomic>
#include <csignal>
#include <future>
#include <iostream>
#include <thread>

#ifdef TESTING
#define SERVER_PATH_LOG "logs/server.log"
#define SERVER_PATH_CONFIG "config/server_parameters.json"
#else
#define SERVER_PATH_LOG "/var/log/dhl_server/server.log"
#define SERVER_PATH_CONFIG "/etc/dhl_server/server_parameters.json"
#endif

int main()
{
    try
    {
        asio::io_context io_context;

        config server_config_params = config::load_config_from_file(SERVER_PATH_CONFIG);
        log_init(SERVER_PATH_LOG, "SERVER");
        set_log_level(LOG_LEVEL_DEBUG);

        server s(io_context, server_config_params); // Crear la instancia del servidor

        // Configurar el manejo de señales usando asio
        asio::signal_set signals(io_context, SIGTERM, SIGINT);
        signals.async_wait([&](auto, auto) {
            std::cout << "Shutting down server gracefully...\n" << std::flush;
            s.shutdown(); // Cerrar todas las conexiones de manera ordenada
            io_context.stop();
        });

        // Ejecutar el io_context en el hilo principal
        io_context.run();

        std::cout << "Server stopped.\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
