# include "../../headers/Commands/UserCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

bool	UserCommand::needsRegistration() const { return false; }

size_t	UserCommand::minParams() const { return 4; }

void	UserCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (user.getAuthState())
		throw IRCError(462);
	if (!user.getNickState())
		throw IRCError(451);

	const std::string&  username = msg.params[0].value;
	const std::string& realname = msg.params[3].value;

	if (username.empty() || realname.empty())
		throw IRCError(461, "");
	
	user.setUsername(username);
	user.setRealname(realname);
	user.setAuthState(true);

	sv.sendTo(user, ":ircserv 001 " +
					user.getNickname() +
					" :Welcome to the Internet Relay Network " +
					user.prefix() + "\r\n");
}
