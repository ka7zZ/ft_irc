#include "irc.hpp"

static bool	isLetter(unsigned char c)
{
	return (std::isalpha(c));
}

static bool isNumber(unsigned char c)
{
        return (std::isdigit(c));
}

static bool isSpecial(unsigned char c)
{

	const std::string specials = "[]\\`_^{|}"; // special characters that can be in the first letter of the nickname || alphabet letters 
	
	
	return  (specials.find(c) != std::string::npos);
}




static bool    validate_args(std::vector<std::string>& args)
{
	std::string nickname ;
        
	if (args.empty())
        {
                // send error 431 is ERR_NONICKNAMEGIVEN 
                return (false) ;
        }
	if (args[0].length() == 1 && args[0][0] == ':') 
	{
                 
                // send error 431 is ERR_NONICKNAMEGIVEN 
                return (false) ;
        }
	bool	limitOk =  (args[0][0] == ':') ? args[0].length() > 10 : args[0].length() > 9;
	if (limitOk)
	{
		// send error 432 ERR_ERRONEUSNICKNAME
		return (false);
	}
	
	//:bob
	//
	nickname = args[0];
	size_t i  = 0;
	bool check_firstcharacter = false;
	if (nickname[i] == ':')
		i++;

	for (; i < nickname.size(); i++)
	{
		
		if (!check_firstcharacter)
		{
			check_firstcharacter = true;
			if (isLetter(nickname[i])) continue; 
			else if (isSpecial(nickname[i])) continue;
			else
			{
				// send 432 ERR_ERRONEUSNICKNAME
				return (false);
			}
		}
		else
		{
			if (isLetter(nickname[i])) continue;
			else if (isSpecial(nickname[i])) continue;
			else  if (isNumber(nickname[i])) continue;
			else  if (nickname[i] == '-') continue;
			else
			{
				// send 432 ERR_ERRONEUSNICKNAME
				return (false);
			}

		}

	}

        return (true);
}

void	Server::handleNick(Client& client, std::vector<std::string>& args)
{
	if (!validate_args(args))
	{
		return ;
	}
	if (args[0][0] == ':')
		args[0].erase(0, 1);


	// example john

	// std::mapv<string, int> nicknames_to_fd; "john" => 4 fd
	
	
	std::string lower_new_nickname(args[0].size(), '\0');	
	
	std::transform(args[0].begin(), args[0].end(), lower_new_nickname.begin(), ::tolower);	
	
	std::map<std::string, int>::iterator it = nicknames_to_fd.find(lower_new_nickname);
	if (it == nicknames_to_fd.end()) // nickname does not exist and work for both  
	{ // even if the nickname is empty or not of the client
		if (!client.get_nickSet()) // if this is the first time i have a nickname
		{
			nicknames_to_fd.insert(std::make_pair(lower_new_nickname, client.get_fd())); // insert lower nickname	
			client.setNickSet(true);
			if (client.get_passOk() & client.get_userSet())
				welcome_msg(client);

		}
		else // assign a different nickname
		{
			std::string old_nickname = client.get_nickname();
			nicknames_to_fd.insert(std::make_pair(lower_new_nickname, client.get_fd())); // insert lower nickname
			nicknames_to_fd.erase(old_nickname);
				
			client.get_outbuffer() += ":" + old_nickname + "!user@host" + " NICK " + ":" + args[0] + "\r\n";
		}
		 
		
	
		client.set_nickname(args[0]); // clients object is filled with it is nickname
		client.setNickSet(true);
			
	}
	else  // the client has already a nickname
	{
		if (it->second != client.get_fd()) //client x want the nickname of another client z
		{
			// send 433  ERR_NICKNAMEINUSE
		}
		return;
	}
}

