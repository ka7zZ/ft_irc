#include "./demo_sources/headers/Server.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    try
    {
        Server server(std::atoi(argv[1]));
        server.run();
    }
    catch (const std::exception &error)
    {
        std::cerr << "server error: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
