#include "irc.hpp"

static bool	validate_args(std::vector<std::string>& args)
{
	if (args.empty())
	{
		// send error 461 ERR_NEEDMOREPARAMS 
		return (false) ;
	}
	if (args[0].length() == 1 && args[0][0] == '#')
	{
		// send error 403 ERR_NOSUCHCHANNEL
		return (false);
	}
	if (args[0].length() < 2)
	{	
		
		// send error 403 ERR_NOSUCHCHANNEL
		return (false);
	}
	return (true);
}

void	Server::handleJoin(Client& client, std::vector<std::string>& args)  // #foot
{
	if (!validate_args(args))
	{
		// stop not accept this command + and maybe i can close teh connection with it
		return ;
	}
	args[0].erase(0, 1); // remove the #
			
	std::map<std::string, Channel>::iterator it = channels.find(args[0]);
	if ( it != channels.end()) // if the channel already exist
	{
	//	if (it)
		{
			std::cerr << 	"arach" << std::endl;	
		}
		// check the flag mode for pass key		
	}
	else   // the channel not yet created
	{
		// create new the channel
		//add the // "football" => {"football", Channel_ object}

		channels.insert(std::make_pair(args[0], Channel(args[0])));
		// add that client to the memebers + make him admin
		Member m = {&client, true};
		channels[args[0]].get_members().insert(std::make_pair(client.get_fd(), m));
		
		// send the reply to the outbuffer in object client
		// :bob!bob@localhost JOIN #football
		client.get_outbuffer() += ":" + client.get_nickname() + "!" + client.get_username() + "@localhost " + "JOIN " + args[0] + "\r\n"; 			
	}
}
