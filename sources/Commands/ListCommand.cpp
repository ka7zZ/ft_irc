# include "../../headers/Commands/ListCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include <sstream>

void	ListCommand::splitParams(const std::string& list, std::vector<std::string>& out)
{
	size_t	start = 0;

	while (start <= list.size())
	{
		size_t	comma = list.find_first_of(',', start);
		if (comma == std::string::npos)
			comma = list.size();
		if (comma > start)
			out.push_back(list.substr(start, comma - start));
		start = comma + 1;
	}
}

bool	ListCommand::isRequested(const std::vector<std::string>& requested,
	const std::string& channelName)
{
	if (requested.empty())
		return true;
	for (size_t i = 0; i < requested.size(); ++i)
	{
		if (requested[i] == channelName)
			return true;
	}
	return false;
}

bool	ListCommand::needsRegistration() const { return true; }

size_t	ListCommand::minParams() const { return 0; }

void	ListCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	std::vector<std::string>	requested;
	if (!msg.params.empty())
		splitParams(msg.params[0].value, requested);

	sv.sendTo(user, ":ircserv 321 " + user.getNickname()
		+ " Channel :Users Name\r\n");

	const std::map<std::string, Channel>& channels = sv.getAllChannels();
	std::map<std::string, Channel>::const_iterator it;
	for (it = channels.begin(); it != channels.end(); ++it)
	{
		if (!isRequested(requested, it->second.getName()))
			continue ;
		const std::string& modes = it->second.getModes();
		if (!it->second.isMember(user.getSocket()) && 
			(modes.find('s') != std::string::npos || modes.find('p') != std::string::npos))
			continue ;
		std::ostringstream	reply;
		reply << ":ircserv 322 " << user.getNickname() << " "
			<< it->second.getName() << " " << it->second.getChannelSize();
		if (it->second.getLimit() > 0)
			reply << "/" << it->second.getLimit();
		reply << " :" << it->second.getTopic();
		sv.sendTo(user, reply.str() + "\r\n");
	}
	sv.sendTo(user, ":ircserv 323 " + user.getNickname() + " :End of /LIST\r\n");
}
