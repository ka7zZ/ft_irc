#include "irc.hpp"

static bool    validate_args(std::vector<std::string>& args)
{
        if (args.empty() || args.size() < 4)
        {
                // send error 461 ERR_NEEDMOREPARAMS 
                return (false);
        }

	if (!args[3].empty() && args[3].length() == 1 && args[3][0] == ':')
	{
                // send error 461 ERR_NEEDMOREPARAMS 
                return (false);
        }
	
	for (size_t i = 0; i < args[3].length(); i++)
	{
    		if (args[3][i] == '\0')
		       	args[3][i] = ' ';
	}

	for (size_t i = 0; i < args[0].length(); i++)
	{
    		if (args[0][i] == '@' || args[0][i] == '\0')
		       	args[0][i] = 'A';
	}
	return (true);
}



void	Server::handleUser(Client& client, std::vector<std::string>& args)
{

	if (client.get_passOk() & client.get_userSet() & client.get_nickSet()) // must be full regisred ==> all true ,  to output this error 
	{

		// send 462 ERR_ALREADYREGISTRED
		return ;
	}
	
	if (!validate_args(args))
	{
		return ;
	}
	if (args[3][0] == ':')
		args[3].erase(0, 1);
	// example {":badr ahmad"}
	
	client.set_username(args[0]);

	client.set_realname(args[3]);
	client.setUserSet(true);
	
	
	if (client.get_passOk() & client.get_nickSet())
	{
		welcome_msg(client);
	}

}
