# include "../../headers/Commands/NickCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

bool	NickCommand::isLegit(const std::string& nick)
{
	const std::string	forbiddenChars = " ,.!*@";
	const std::string	forbiddenStart = "1234567890-#&$:";

	if (forbiddenStart.find(nick[0]) != std::string::npos)
		return false;
	for (size_t i = 0; i < nick.size(); i++) {
		if (forbiddenChars.find(nick[i]) != std::string::npos)
			return false;
	}
	return true;
}

bool	NickCommand::needsRegistration() const { return false; }

size_t	NickCommand::minParams() const { return 1; }

void	NickCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (user.getPassState() == false)
		throw IRCError(451);

	const std::string&	nick = msg.params[0].value;

	if (nick.empty())
		throw IRCError(431);
	if (!isLegit(nick))
		throw IRCError(432, nick);

	Client* other = sv.getClientByNickname(nick);
	if (other != NULL && other->getSocket() != user.getSocket())
		throw IRCError(433, nick);
	if (nick == user.getNickname())
		return ;

	bool	alreadyRegistered = user.getAuthState();
	std::string	oldPrefix = user.prefix();

	user.setNickname(nick);
	user.setNickState(true);

	if (alreadyRegistered) {
		sv.broadcastAll(&user, ":" + oldPrefix + " NICK :" + nick + "\r\n");
	}
	else if (user.getPassState() && user.getUserState())
	{
		user.setAuthState(true);
		sv.sendTo(user, ":ircserv 001 " + user.getNickname() + " :Welcome to the Internet Relay Network " + user.prefix() + "\r\n");
		sv.sendTo(user, ":ircserv 002 " + user.getNickname() + " :Your host is ircserv, running version ft_irc\r\n");
		sv.sendTo(user, ":ircserv 003 " + user.getNickname() + " :This server was created by two 42 students\r\n");
		sv.sendTo(user, ":ircserv 004 " + user.getNickname() + " ircserv ft_irc i ibklmnopstv\r\n");
	}
}
