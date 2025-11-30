#include "server.hpp"
#include <asio.hpp>
#include <csignal>
#include <iostream>

int main()
{
    try
    {
        asio::io_context io_context;
        server srv(io_context);

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

