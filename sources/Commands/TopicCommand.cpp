# include "../../headers/Commands/TopicCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

bool	TopicCommand::needsRegistration() const { return true; }

size_t	TopicCommand::minParams() const { return 1; }

void	TopicCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	const std::string&	channelName = msg.params[0].value;
	Channel*	channel = sv.getChannel(channelName);

	if (channel == NULL)
		throw IRCError(403, channelName);
	if (!channel->isMember(user.getSocket()))
		throw IRCError(442, channelName);

	if (msg.params.size() == 1)
	{
		if (channel->getTopic().empty())
			sv.sendTo(user, ":ircserv 331 " + user.getNickname() + " " + channelName + " :No topic is set\r\n");
		else
			sv.sendTo(user, ":ircserv 332 " + user.getNickname() + " " + channelName + " :" + channel->getTopic() + "\r\n");
		return ;
	}
	if (channel->isTopicOperatorOnly() && !channel->isAdmin(user.getSocket()))
		throw IRCError(482, channelName);
	channel->setTopic(msg.params[1].value);
	sv.broadcast(*channel, ":" + user.prefix() + " TOPIC " + channelName + " :" + channel->getTopic() + "\r\n", -1);
}
