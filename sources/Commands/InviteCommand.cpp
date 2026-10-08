# include "../../headers/Commands/InviteCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"


bool	InviteCommand::needsRegistration() const { return true; }

size_t	InviteCommand::minParams() const { return 2; }

void	InviteCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	const std::string&	nick = msg.params[0].value;
	const std::string&	ch = msg.params[1].value;

	Channel*	channel = sv.getChannel(ch);
	if (channel == NULL)
		throw IRCError(403);
	if (!channel->isMember(user.getSocket()))
		throw IRCError(442, channel->getName());
	if (!channel->isAdmin(user.getSocket()))
		throw IRCError(482, user.getNickname());

	Client*	target = sv.getClientByNickname(nick);
	if (target == NULL)
		throw IRCError(401, msg.params[0].value);
	
	if (channel->getMember(target->getSocket()))
		throw IRCError(443, target->getNickname());
	
	channel->addInvited(target->getSocket());
	sv.sendTo(user, ":ircserv 341 " + user.getNickname() + " " + nick + " " + ch + "\r\n");
	sv.sendTo(*target, ":" + user.prefix() + " INVITE " + nick + " " + ch + "\r\n");
}
