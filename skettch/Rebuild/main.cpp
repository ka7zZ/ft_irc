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
		const char  msg[] = "\nSIGINT: Shutting down...\n";
		write(STDOUT_FILENO, msg, sizeof(msg) - 1);
		g_serverRunning = 0;
	}
}

bool	requiredConditions(int ac, char **argv) {
	if (argc != 3 || !argv[2] || !argv[2][0])
	{
		std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
		return false;
	}
	int port = std::atoi(argv[1]);
	if (port <= 0 || port > 65535)
	{
		std::cerr << "[ERROR]: The <port> must be a number between 1 and 65535.\n" << std::endl;
		return false;
	}
	return true;
}

int main(int argc, char** argv)
{
	if (!requiredConditions(argc, argv))
		return -1;
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signalHandler);
	try
	{
		Server sv(std::atoi(argv[1]), argv[2]);
		sv.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "ERROR: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}