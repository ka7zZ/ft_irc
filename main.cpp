#include "Server.hpp"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

volatile sig_atomic_t g_serverRunning = 1;

static void	handleSignal(int)
{
	g_serverRunning = 0;
}

int	main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
		return EXIT_FAILURE;
	}

	char	*end = NULL;
	long	port = std::strtol(argv[1], &end, 10);
	if (*argv[1] == '\0' || *end != '\0' || port < 1 || port > 65535)
	{
		std::cerr << "Error: port must be an integer between 1 and 65535."
			<< std::endl;
		return EXIT_FAILURE;
	}

	std::signal(SIGINT, handleSignal);
	std::signal(SIGTERM, handleSignal);

	try
	{
		Server server(static_cast<int>(port), argv[2]);
		server.run();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Error: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
