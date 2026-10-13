# include "../../headers/Commands/PingCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/IRCError.hpp"

bool	PingCommand::needsRegistration() const { return true; }

size_t	PingCommand::minParams() const { return 0; }

void	PingCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (msg.params.empty())
		throw IRCError(409);
	sv.sendTo(user, ":ircserv PONG ircserv :" + msg.params[0].value + "\r\n");
}