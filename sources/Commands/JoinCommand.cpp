#include "../../headers/Commands/JoinCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"


void	JoinCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

bool	JoinCommand::validName(const std::string& channelName)
{
	if (channelName.size() < 2 || channelName.size() > 50 || channelName[0] != '#')
		return false;
	const std::string&	forbidden = " ,\a\r\n\0";
	for (size_t i = 0; i < channelName.size(); i++)
	{
		if (forbidden.find_first_of(channelName[i]) != std::string::npos)
			return false;
	}
	return true;
}

bool	JoinCommand::freeSpace(size_t limit, size_t members) { return limit > members; }

void	JoinCommand::sendReplies(Server& sv, Client& cl, const Channel& ch)
{
	const std::string&	nick = cl.getNickname();

	if (ch.getTopic().empty())
		sv.sendTo(cl, ":ircserv 331 " + nick + " " + ch.getName() + " :No topic is set\r\n");
	else
		sv.sendTo(cl, ":ircserv 332 " + nick + " " + ch.getName() + " :" + ch.getTopic() + "\r\n");
	
	std::string names;
	const std::map<int, Member>& members = ch.getAllMembers();
	for (std::map<int, Member>::const_iterator it = members.begin(); it != members.end(); ++it)
	{
		if (!names.empty())
			names += " ";
		if (it->second.isAdmin)
			names += "@";
		names += it->second.cl->getNickname();
	}
	sv.sendTo(cl, ":ircserv 353 " + nick + " = " + ch.getName() + " :" + names + "\r\n");
	sv.sendTo(cl, ":ircserv 366 " + nick + " " + ch.getName() + " :End of /NAMES list.\r\n");
}

void	JoinCommand::joinChannel(Server& sv, Client& cl, const std::string& name, const std::string& key)
{
	if (!validName(name))
		throw IRCError(403, name);
	Channel* ch = sv.getChannel(name);
	if (ch == NULL) {
		sv.createChannel(&cl, name);
		ch = sv.getChannel(name);
	}
	else
	{
		if (ch->isMember(cl.getSocket()))
			return ;
		if (ch->getLimit() != 0 && !freeSpace(ch->getLimit(), ch->getChannelSize()))
			throw IRCError(471, name);
        bool    isInvited = ch->isInvited(cl.getSocket());
		if (ch->isInviteOnly() && !isInvited)
			throw IRCError(473, name);
        bool    isBanned = ch->isBanned(cl.prefix());
        if (isBanned)
            throw IRCError(474, cl.getNickname());
		if (!ch->getKey().empty() && ch->getKey() != key)
			throw IRCError(475, name);
		ch->addMember(&cl, false, false);
		ch->removeInvited(cl.getSocket());
	}
	sv.broadcast(*ch, ":" + cl.prefix() + " JOIN " + ch->getName() + "\r\n", -1);
	sendReplies(sv, cl, *ch);
}

bool	JoinCommand::needsRegistration() const { return true; }

size_t	JoinCommand::minParams() const { return 1; }

void	JoinCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	std::vector<std::string>	channels;
	std::vector<std::string>	keys;

	splitParams(msg.params[0].value, channels);
	if (msg.params.size() > 1)
		splitParams(msg.params[1].value, keys);
	
	for (size_t i = 0; i < channels.size(); i++) {
		try {
			if (!keys.empty() && i < keys.size())
				joinChannel(sv, user, channels[i], keys[i]);
			else
				joinChannel(sv, user, channels[i], "");
		} catch (const IRCError& e) {
			sv.sendTo(user, e.format(user.getNickname()));
		}
	}
}
