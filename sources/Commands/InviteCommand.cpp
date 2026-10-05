# include "../../headers/Commands/InviteCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

void	InviteCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

bool	InviteCommand::needsRegistration() const { return true; }

size_t	InviteCommand::minParams() const { return 2; }

void	InviteCommand::execute(Server& sv, Client& user, const Message& msg) const
{

	if (msg.params.size() != minParams())
		throw IRCError(461, msg.command);
	if (msg.params[0].value.find_first_of('#') != std::string::npos)
		throw IRCError(401, msg.params[0].value);

	const std::string&	userNick = msg.params[0].value;
	const std::string&	channel = msg.params[1].value;
	if ()
	
}

