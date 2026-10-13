# include "../../headers/Commands/KickCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/IRCError.hpp"

void	KickCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

void	KickCommand::kickOne(Server& sv, Client& op, const std::string& chanName, const std::string& nick, const Message& msg)
{
	Channel* ch = sv.getChannel(chanName);
	if (ch == NULL)
		throw IRCError(403, chanName);
	if (!ch->isMember(op.getSocket()))
		throw IRCError(442, chanName);
	if (!ch->isAdmin(op.getSocket()))
		throw IRCError(482, chanName);

	Client* target = sv.getClientByNickname(nick);
	if (target == NULL)
		throw IRCError(401, nick);
	if (!ch->isMember(target->getSocket()))
		throw IRCError(441, nick + " " + chanName);

	std::string	reason = op.getNickname();
	if (msg.params.size() > 2)
		reason = msg.params[2].value;
	sv.broadcast(*ch,
		":" + op.prefix() + " KICK " + ch->getName() + " " + target->getNickname() + " :" + reason + "\r\n",
		-1
	);
	ch->removeMember(target->getSocket());
}

bool	KickCommand::needsRegistration() const { return true; }

size_t	KickCommand::minParams() const { return 2; }

void	KickCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	std::vector<std::string>	channels;
	splitParams(msg.params[0].value, channels);
	std::vector<std::string>	users;
	splitParams(msg.params[1].value, users);

	if (channels.empty() || users.empty())
		throw IRCError(461, "KICK");
	if (channels.size() != 1 && channels.size() != users.size())
		throw IRCError(461, "KICK");

	for (size_t i = 0; i < users.size(); ++i)
	{
		std::string& chan = channels[0];
		if (channels.size() > 1)
			chan = channels[i];
		try {
			kickOne(sv, user, chan, users[i], msg);
		} catch (const IRCError& e) {
			sv.sendTo(user, e.format(user.getNickname()));
		}
	}
}