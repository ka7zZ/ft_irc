#include "irc.hpp"

static bool    validate_args(Client& client, std::vector<std::string>& args)
{
        if (args.empty())
        {
                // send error 461 ERR_NEEDMOREPARAMS
		std::vector<std::string> params;
		params.push_back("PASS");
		handleError(client, "ERR_NEEDMOREPARAMS", params); 
                return (false);
        }
	if (!args[0].empty() && args[0].length() == 1 && args[0][0] == ':')
	{
                // send error 461 ERR_NEEDMOREPARAMS 
                std::vector<std::string> params;
		params.push_back("PASS");
		handleError(client, "ERR_NEEDMOREPARAMS", params); 
                return (false);

        }

        return (true);
}

void	Server::handlePass(Client& client, std::vector<std::string>& args)
{

	if (client.get_passOk() & client.get_userSet() & client.get_nickSet()) // must be full regisred ==> all true ,  to output this error 
	{
		
		// send 462 ERR_ALREADYREGISTRED
		std::vector<std::string> params;
		handleError(client, "ERR_ALREADYREGISTERED", params);
		return ;
	}
	
	if (!validate_args(client, args))
	{
		return ;
	}
	if (args[0][0] == ':')
		args[0].erase(0, 1);
	// example {":123"}
	if (password != args[0])
	{
		// send 464 ERR_PASSWDMISMATCH
		std::vector<std::string> params;
		handleError(client, "ERR_PASSWDMISMATCH", params);
		client.setPassOk(false);
		return ;
	}
	client.setPassOk(true);
	if (client.get_userSet() & client.get_nickSet())
		welcome_msg(client);

}
