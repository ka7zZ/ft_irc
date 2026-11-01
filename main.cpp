#include "Server.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sstream>

volatile sig_atomic_t g_serverRunning = 1;

static void	handleSignal(int)
{
	g_serverRunning = 0;
}

static void validatePort(const char* port, size_t *p)
{
	char c;
	std::stringstream ss(port);
	ss >> *p;
	if (ss.fail())
		throw std::runtime_error("port format must contain only digits");
	if (ss >> c)
		throw std::runtime_error("port format must  contain only digits");
	if (*p < 1024 || *p > 65535)
		throw std::runtime_error("port number must be between 1024 and 65535");
}



int	main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
		return EXIT_FAILURE;
	}
	std::signal(SIGINT, handleSignal);
	std::signal(SIGTERM, handleSignal);
	std::signal(SIGPIPE, SIG_IGN);
	size_t port;
	try
	{
		validatePort(argv[1], &port);
		Server server(port, argv[2]);
		server.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}