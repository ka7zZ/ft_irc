#include <iostream>
#include <cstdlib>
#include <csignal>
#include "../headers/Server.hpp"

Server* globalServer = NULL;

// Handler pentru CTRL+C (SIGINT)
void signalHandler(int signum)
{
    std::cout << "\n[SIGNAL: " << signum << " RECEIVED] Shutting down the server..." << std::endl;
    if (globalServer != NULL)
    {
        globalServer->stop();
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
    Server server(port);
    globalServer = &server;
    signal(SIGINT, signalHandler);
    try
    {
        server.start();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR MAIN]: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Successfully shutdown" << std::endl;
    return 0;
}