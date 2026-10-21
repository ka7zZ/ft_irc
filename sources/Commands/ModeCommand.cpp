# include "../../headers/Commands/ModeCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"
/*
Syntax: MODE <target> [flags] [params...]
Actions:
	+ - add mode
	- - remove mode

===============================================================
1. GLOBAL USER MODES (Applied by the user on themselves)
===============================================================
	-/+i - invisible / visible (hidden from public WHO/WHOIS queries)
		Example: MODE MyNickname +i

	-/+o - add / remove global Server Operator (IRCop) privileges
		Example: MODE MyNickname -o (Usually added via OPER command)

	-/+w - receive / ignore global WALLOPS messages
		Example: MODE MyNickname +w

	-/+s - receive / ignore Server Notices
		Example: MODE MyNickname +s

===============================================================
2. CHANNEL MODES (Applied to a specific channel)
===============================================================
	[Requires NO parameters]
	-/+m - moderated / unmoderated (only users with +o or +v can send messages)
		Example: MODE #channel +m

	-/+i - invite-only / public (requires INVITE command to join)
		Example: MODE #channel +i

	-/+s - secret / public (channel is hidden from the LIST command)
		Example: MODE #channel +s

	-/+p - private / public (similar to secret, but handled differently in WHOIS)
		Example: MODE #channel +p

	-/+t - topic lock / unlock (only channel operators can change the topic)
		Example: MODE #channel +t

	-/+n - no external messages (blocks messages from users who are not in the channel)
		Example: MODE #channel +n

	[Requires parameter ONLY on add (+)]
	-/+l <limit> - sets / removes the maximum number of users allowed
		Example (Add): MODE #channel +l 50
		Example (Remove): MODE #channel -l

	-/+k <password> - sets / removes the channel password (key)
		Example (Add): MODE #channel +k secret123
		Example (Remove): MODE #channel -k (Some servers require '-k secret123')

===============================================================
3. CHANNEL MEMBER STATUS (Applied by an operator to a specific user in the channel)
===============================================================
	[Requires parameter for BOTH (+) and (-)]
	-/+o <nickname> - gives / takes away Channel Operator status
		Example (Give): MODE #channel +o John
		Example (Take): MODE #channel -o John

	-/+v <nickname> - gives / takes away Voice status (right to speak when +m is active)
		Example: MODE #channel +v Alex

	-/+b <mask/nick> - adds / removes a ban on the channel
		Example (Ban by mask): MODE #channel +b *!*@*.badhost.com
		Example (Ban by nick): MODE #channel +b SpammerNick
		Example (Remove ban): MODE #channel -b *!*@*.badhost.com

===============================================================
4. READ-ONLY QUERY MODES (Used to request information from the server)
===============================================================
	MODE #channel    - returns the currently active channel flags (e.g., +mnt)
		Example: MODE #channel

	MODE #channel +b - returns the list of all active ban masks on that channel
		Example: MODE #channel +b

*/

void	ModeCommand::throwErrors(Server& sv, Client& user, const Message& msg)
{
	const std::string   availableOptions = "biklmnopstvw";
	const std::string&	target = msg.params[0].value;

	if (!sv.getClientByNickname(target))
		throw IRCError(401, target);
	else if (sv.getClientByNickname(target)->getNickname() != user.getNickname())
		throw (IRCError(502, target));
	if (msg.params.size() == 1)
		return ;
	const std::string&	flags = msg.params[1].value;
	
}

void	ModeCommand::sendMessage(Server)
{

}

const std::string&	ModeCommand::messageBanMode(Server& sv, Client& user, Flag f) const
{
	std::string	msg;

	if (f.act == ADD) {
		if (f.parameterValue == NULL)
			throw IRCError()

	}
	if (f.act == REMOVE) {

	}
}

void	ModeCommand::parseFlags(Server& sv, Client& user, Channel* ch, const Message& msg)
{
	std::vector<Flag>::iterator	it;

	for (it = _flags.begin(); it != _flags.end(); ++it) {
		switch (it->flag) {
			case 'b':
				if (msg.params.size() == 1)
					sv.sendTo(user, ch->getChannelMode() + "\r\n");
				else
					sv.broadcast(*ch, messageBanMode(sv, user, it) + "\r\n", -1);
				break ;
			case 'i':
				break ;
			case 'k':
				break ;
			case 'l':
				break ;
			case 'm':
				break ;
			case 'n':
				break ;
			case 'o':
				break ;
			case 'p':
				break ;
			case 's':
				break ;
			case 't':
				break ;
			case 'v':
				break ;
		}
	}
}

bool	ModeCommand::isWithin(const std::string& flags, char f)
{
	return flags.find_first_of(f) != std::string::npos;
}

void	ModeCommand::getFlagArray(const Message& msg, std::vector<Flag> flags)
{
	const std::string&	inputFlags = msg.params[1].value;
	size_t	index = 0;
	size_t	paramPos = 1;

	while (index < inputFlags.length())																		//	GOING THRU THE PARSED FLAGS
	{
		Action	action;																					//	DEFINE THE ACTION OF THE FLAG
		action = flags[index] == '+' ? ADD : REMOVE;
		index++;																						// 	MOVE TO THE NEXT CHAR
		while (index < inputFlags.length() && isWithin("biklmnrstv", inputFlags[index])) {						//	AGGREGATE FLAGS WHILE IS WITHING PERMITTED FLAGS
			Flag	f;
			f.act = action;
			f.flag = inputFlags[index];
			f.isParameterized = false;
			f.parameterValue = NULL;
			if (isWithin("bki", inputFlags[index]) &&														//	CHECK IF IS WITHIN THE PARAMETERIZED FLAGS
				(f.act == ADD ||																		//	AND IS ONE THAT CHANGES CHANNEL SETTINGS
					(f.act == REMOVE && inputFlags[index] == 'b'))) {										//	OR IS THE REMOVE BAN THAT NEEDS PARAMETER OF WHOM
				f.parameterValue = ++paramPos < msg.params.size() ? &msg.params[paramPos].value : NULL;											//	ASSIGNING THE POSITION OF THE PARAMETER IN THE ASCEND ORDER
				f.isParameterized = true;
			}
			flags.push_back(f);
			index++;
		}
	}
}

void	ModeCommand::channelMode(Server& sv, Client& user, const Message& msg, std::vector<Flag> flags)
{
	const std::string&	target = msg.params[0].value;
	// CHANNEL EXISTS
	Channel* channel = sv.getChannel(msg.params[0].value);
	if (!channel)
		throw IRCError(403, target);
	// MODE #channel
	if (msg.params.size() == 1) {
		sv.sendTo(user, channel->getChannelMode());
		return ;
	}
	const std::string&	inputFlags = msg.params[1].value;
	if (flags.size() < 2 || (inputFlags[0] != '+' && inputFlags[0] != '-'))
		throw IRCError(472, user.getNickname());
	getFlagArray(msg, flags);
	parseFlags(sv, user, channel, msg);
}

void	ModeCommand::userMode(Server& sv, Client& user, const Message& msg)
{

}

void	ModeCommand::memberStatus(Server& sv, Client& user, const Message& msg)
{

}

bool	ModeCommand::needsRegistration() const { return false; }

size_t	ModeCommand::minParams() const { return 1; }

void	ModeCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	std::vector<Flag>	flags;
	try {
		if (msg.params.empty() || msg.params[0].value.empty())
			throw IRCError(461);
		if (msg.params[0].value[0] == '#')
			channelMode(sv, user, msg, flags);
	} catch (IRCError& e) {
		sv.sendTo(user, e.format(user.getNickname()));
	}
}