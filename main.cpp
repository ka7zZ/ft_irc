#include "irc.hpp"


int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "./serv <port> <password>" << std::endl;
		return (1);
	}
	int port = std::atoi(argv[1]);
	 
	Server ser(port, argv[2]);
       return (0);	
}
