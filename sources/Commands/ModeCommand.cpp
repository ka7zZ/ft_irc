# include "../../headers/Commands/ModeCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"
# include <cstdlib>
# include <sstream>

void	ModeCommand::sendMessage(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	std::string	message = ":" + user.prefix() + " MODE " + ch.getName();
	if (f.act == ADD)
		message += " +";
	else
		message += " -";
	message += f.flag;
	if (f.isParameterized)
		message += " " + *f.parameterValue;
	sv.broadcast(ch, message + "\r\n", -1);
}

void	ModeCommand::userVisibleOpt(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	(void)ch;
	if (f.act == ADD) {
		if (user.isInvisible())
			return;
		user.setInvisible(true);
		sv.sendTo(user, user.prefix() + " MODE +i\r\n");
		return ;
	}
	if (!user.isInvisible())
		return ;
	user.setInvisible(false);
	sv.sendTo(user, user.prefix() + " MODE -i\r\n");
}

void	ModeCommand::userBanOpt(Server& sv, Client& user, Channel& ch)
{
	const std::vector<BanFormat>&	bans = ch.getBanList();
	std::vector<BanFormat>::const_iterator it;

	for (it = bans.begin(); it != bans.end(); ++it) {
		sv.sendTo(user, ":ircserv 367 " + user.getNickname() + " " + ch.getName() + " " + ch.formattedBan(*it) + "\r\n");
	}

	sv.sendTo(user, ":ircserv 368 " + user.getNickname() + " " + ch.getName() + " :End of channel ban list\r\n");
}

void	ModeCommand::sendChannelDetails(Server& sv, Client& user, Channel& ch)
{
	std::string	channelDetails = ch.getName() + " " + ch.getModes();
	if (!ch.getKey().empty())
		channelDetails += " " + ch.getKey();
	if (ch.getLimit() > 0) {
		std::ostringstream	oss;
		oss << ch.getLimit();
		channelDetails += " " + oss.str();
	}
	sv.sendTo(user, ":ircserv 324 " + user.getNickname() + " " + channelDetails + "\r\n");
}

void	ModeCommand::banOpt(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	if (f.act == ADD && !f.isParameterized) {
		userBanOpt(sv, user, ch);
		return ;
	}
	if (f.parameterValue == NULL)
		throw IRCError(461, "MODE");
	if (!ch.isAdmin(user.getSocket()))
		throw IRCError(482, ch.getName());
	Client *who = sv.getClientByPrefix(*f.parameterValue);
	if (!who)
		return ;
	char	banType = (f.act == ADD) ? '+' : '-';
	if (banType == '-')
		ch.removeBan(*f.parameterValue);
	else if (!ch.banMember(*f.parameterValue, user.prefix()))
		return ;
	sv.broadcast(ch, ":" + user.prefix() + " " + "MODE " + ch.getName() + " " + banType + f.flag + " " + *f.parameterValue + "\r\n", -1);
}

void	ModeCommand::keyOpt(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	if (f.act == ADD) {
		if (!f.isParameterized || f.parameterValue->empty())
			throw IRCError(461, user.getNickname());
		const std::string& key = *f.parameterValue;
		if (key.size() > 23 || key.find_first_of(" ,:") != std::string::npos)
			throw IRCError(525, ch.getName());
		ch.setKey(*f.parameterValue);
		sv.broadcast(ch, ":ircserv " + user.prefix() + " MODE +k " + *f.parameterValue + "\r\n", -1);
		return ;
	}
	ch.setKey("");
	sv.broadcast(ch, ":ircserv " + user.prefix() + " MODE -k *\r\n", -1);
}

void	ModeCommand::limitOpt(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	if (f.act == REMOVE)
	{
		if (ch.getLimit() == 0)
			return ;
		ch.setLimit(0);
		sendMessage(sv, user, ch, f);
		return ;
	}
	if (!f.isParameterized || f.parameterValue->empty())
		throw IRCError(461, "MODE");
	long limit = std::atoi(f.parameterValue->c_str());
	if (ch.getLimit() == static_cast<size_t>(limit))
		return ;
	ch.setLimit(static_cast<size_t>(limit));
	sendMessage(sv, user, ch, f);
}

void	ModeCommand::memberOpt(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	if (!f.isParameterized)
		throw IRCError(461, "MODE");
	const std::string&	nick = *f.parameterValue;
	Client*	target = sv.getClientByNickname(nick);
	if (target == NULL)
		throw IRCError(401, nick);
	if (!ch.isMember(target->getSocket()))
		throw IRCError(441, nick);
	bool	grant = (f.act == ADD);
	bool	changed;
	if (f.flag == 'o')
		changed = ch.setAdmin(target->getSocket(), grant);
	else
		changed = ch.setVoiced(target->getSocket(), grant);
	if (changed)
		sendMessage(sv, user, ch, f);
}

bool	ModeCommand::takesParam(char flag, Action act)
{
	switch (flag) {
		case 'b' : case 'k': case 'o': case 'v' :
			return true;
		case 'l':
			return act == ADD;
	}
	return false;
}

void	ModeCommand::getFlagArray(const Message& msg, std::vector<Flag>& flags)
{
	const std::string&	inputFlags = msg.params[1].value;
	size_t	paramPos = 2;
	Action	action = ADD;

	for (size_t index = 0; index < inputFlags.length(); ++index)
	{
		if (inputFlags[index] == '+' || inputFlags[index] == '-') {
			action = inputFlags[index] == '+' ? ADD : REMOVE;
			continue ;
		}
		Flag	f;
		f.act = action;
		f.flag = inputFlags[index];
		f.isParameterized = false;
		f.parameterValue = NULL;
		if (takesParam(f.flag, f.act) && paramPos < msg.params.size()) {
			f.parameterValue = &msg.params[paramPos++].value;
			f.isParameterized = true;
		}
		flags.push_back(f);
	}
}

void	ModeCommand::applyFlag(Server& sv, Client& user, Channel& ch, const Flag& f)
{
	switch (f.flag) {
		case 'i' : case 't': case 'm': case 'n': case 'p': case 's':
			if(ch.setModes(f.flag, f.act == ADD))
				sendMessage(sv, user, ch, f);
			break ;
		case 'b':
			banOpt(sv, user, ch, f);
			break ;
		case 'k':
			keyOpt(sv, user, ch, f);
			break ;
		case 'l':
			limitOpt(sv, user, ch, f);
			break ;
		case 'o': case 'v':
			memberOpt(sv, user, ch, f);
			break ;
		default:
			throw IRCError(472, std::string(1, f.flag));
	}
}

void	ModeCommand::parseChannelFlags(Server& sv, Client& user, Channel& ch, std::vector<Flag>& flags)
{
	for (size_t i = 0; i < flags.size(); i++)
	{
		Flag	current = flags[i];
		if (current.flag == 'b' && current.act == ADD && !current.isParameterized)
			userBanOpt(sv, user, ch);
		else {
			if (!ch.isAdmin(user.getSocket()))
				throw IRCError(482, ch.getName());
			try {
				applyFlag(sv, user, ch, current);
			} catch (IRCError& e) {
				sv.sendTo(user, e.format(user.getNickname()));
			}
		}
	}
}

void	ModeCommand::channelMode(Server& sv, Client& user, const Message& msg)
{
	std::vector<Flag>	flags;
	const std::string&	target = msg.params[0].value;
	Channel*	channel = sv.getChannel(target);
	if (!channel)
		throw IRCError(403, target);
	if (msg.params.size() == 1) {
		sendChannelDetails(sv, user, *channel);
		return ;
	}
	if (msg.params[1].value.empty())
		throw IRCError(472, target);
	if (msg.params[1].value[0] != '+' && msg.params[1].value[0] != '-')
		throw IRCError(472, msg.params[1].value);
	getFlagArray(msg, flags);
	parseChannelFlags(sv, user, *channel, flags);
}

void	ModeCommand::userMode(Server& sv, Client& user, const Message& msg)
{
	const std::string	userFlags = "i";
	const std::string&	target = msg.params[0].value;
	if (target != user.getNickname())
		throw IRCError(502);
	if (msg.params.size() == 1) {
		std::string	userDetails = user.getNickname() + " +";
		if (user.isInvisible())
			userDetails += "i";
		sv.sendTo(user, ":ircserv 221 " + userDetails + "\r\n");
		return ;
	}
	const std::string&	flags = msg.params[1].value;
	Action	action = ADD;
	for (size_t i = 0; i < flags.size(); ++i)
	{
		if (flags[i] == '+' || flags[i] == '-') {
			action = flags[i] == '+' ? ADD : REMOVE;
			continue ;
		}
		if (userFlags.find(flags[i]) == std::string::npos)
			throw IRCError(501);
		if (flags[i] == 'i')
		{
			bool wasInvisible = user.isInvisible();
			user.setInvisible(action == ADD);
			if (wasInvisible != user.isInvisible())
				sv.sendTo(user, user.prefix() + " MODE "
					+ (action == ADD ? "+i\r\n" : "-i\r\n"));
		}
	}
}

bool	ModeCommand::needsRegistration() const { return true; }

size_t	ModeCommand::minParams() const { return 1; }

void	ModeCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	try {
		if (msg.params.empty() || msg.params[0].value.empty())
			throw IRCError(461);
		if (msg.params[0].value[0] == '#')
			channelMode(sv, user, msg);
		else
			userMode(sv, user, msg);
	} catch (IRCError& e) {
		sv.sendTo(user, e.format(user.getNickname()));
	}
}
