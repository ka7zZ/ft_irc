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

void	ModeCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

bool	ModeCommand::needsRegistration() const { return false; }

size_t	ModeCommand::minParams() const { return 1; }

void	ModeCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (msg.params.empty())
		throw IRCError(461);
	if (msg.params.size() == 1)
	{
		Channel*  channel = sv.getChannel(msg.params[0].value);
		if (!channel)
			throw IRCError(403, msg.params[0].value);
		sv.sendTo(user, ":" + user.prefix() + " :ircserv 324 " + user.getNickname() + " " + channel->getName());
	}
}