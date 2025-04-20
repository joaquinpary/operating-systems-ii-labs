#include "dhl_server.hpp"
#include <asio.hpp>
#include <csignal>
#include <iostream>
#include <thread>
#include "database.hpp"

int main()
{
    std::cout<<"iniciando sv"<< std::flush;
    try
    {
        const auto processor_count = std::thread::hardware_concurrency();

        asio::io_context io_context;
        asio::thread_pool cpu_pool(processor_count);

        asio::signal_set signals(io_context, SIGINT);
        signals.async_wait([&](auto, auto) { io_context.stop(); });

        server s(io_context); // Create the server instance, initializing all 4 sockets

        asio::post(cpu_pool, [&] { io_context.run(); });

        cpu_pool.join();

        std::cout << "Server stopped.\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
