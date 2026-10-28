#include "Bot.hpp"

int	main(int argc, char **argv)
{

	if (argc != 3)
	{
		std::cerr <<  "usage ./bot <hostname> <port>\n";
		return (0);
	}

	Bot client(argv[1], argv[2]);

	client.prompt_BotInterface();
	client.initializeBot();
	client.connectBot();
	client.login();
	//client.sendRequest();
	client.initServerHandlers();
	client.initUserCommands();
	client.keepWaitingReply();
	
	return (0);

		
}
