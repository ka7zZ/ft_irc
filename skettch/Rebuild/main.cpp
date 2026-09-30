#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <csignal>

volatile sig_atomic_t	g_serverRunning = 1;

// HANDLE CONNECTION TO PREVENT FLOOD ATTACKS
void signalHandler(int signum)
{
    if (signum == SIGINT)
    {
        std::cout << "\n[SIGNAL: " << signum << " RECEIVED] Shutting down the server..." << std::endl;
        g_serverRunning = 0;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }
    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535)
    {
        std::cerr << "[ERROR]: The <port> must be a number between 1 and 65535.\n" << std::endl;
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signalHandler);
    try
    {
        Server sv(port);
        sv.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR MAIN]: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Successfully shutdown" << std::endl;
    return 0;
}