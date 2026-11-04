#include "Bot.hpp"
#include <iostream>
#include <sstream>
#include <csignal>

volatile sig_atomic_t g_running = 1;

static void signalHandler(int signum)
{
    (void)signum;
    g_running = 0;
}

static void validateArgs(const char* address, const char* port)
{
	const std::string& addr = address;
	if (addr != "127.0.0.1")
		throw std::runtime_error("addres must be 127.0.0.1");
	size_t p;
	char c;
	std::stringstream ss(port);
	ss >> p;
	if (ss.fail())
		throw std::runtime_error("port format must contain only digits");
	if (ss >> c)
		throw std::runtime_error("port format must  contain only digits");
	if (p < 1024 || p > 65535)
		throw std::runtime_error("port number must be between 1024 and 65535");
}

int	main(int argc, char **argv)
{

	if (argc != 3)
	{
		std::cerr <<  "usage ./bot <hostname> <port>\n";
		return (0);
	}
	std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
	try
	{
		validateArgs(argv[1], argv[2]);

		Bot bot(argv[1], argv[2]);
		bot.run();
	} catch(std::exception &e)
	{
		std::cout << e.what() << std::endl;
	}
	std::cout << "Bot shutting down..." << std::endl;
	return 0;

	
		
}
