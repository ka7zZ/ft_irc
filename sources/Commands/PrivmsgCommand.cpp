# include "../../headers/Commands/PrivmsgCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"


void	PrivmsgCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

void	PrivmsgCommand::sendOne(Server& sv, Client& user, const std::string& recipient, const std::string& message)
{
	
	if (recipient.empty())
		throw IRCError(411, user.getNickname());
	if (message.empty())
		throw IRCError(412, user.getNickname());
	const std::string line = ":" + user.prefix() + " PRIVMSG " + recipient + " :" + message + "\r\n";
	if (recipient[0] == '#') {
		Channel*	ch = sv.getChannel(recipient);
        if (ch == NULL)
			throw IRCError(403, recipient);
        bool    member = ch->isMember(user.getSocket());
        if (ch->isSecret() && !member)
            throw IRCError(403, recipient);
		if (ch->isBanned(user.prefix())
			|| !member
			|| (ch->isModerated() && !ch->isAdmin(user.getSocket())
				&& !ch->isVoiced(user.getSocket())))
			throw IRCError(404, recipient);
		sv.broadcast(*ch, line, user.getSocket());
		return ;
	}
	Client*	target = sv.getClientByNickname(recipient);
	if (target == NULL)
		throw IRCError(401, recipient);
	sv.sendTo(*target, line);
}

bool	PrivmsgCommand::needsRegistration() const { return true; }

size_t	PrivmsgCommand::minParams() const { return 1; }

void 	PrivmsgCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	
	if (msg.params.size() == 1)
	{
		if (msg.params[0].isTrailling)
			throw IRCError(412, user.getNickname());
		throw IRCError(411, user.getNickname());
	}
	const std::string& message = msg.params[1].value;
	if (msg.params[0].value.find(',') == std::string::npos) {
		const std::string& recipient = msg.params[0].value;
		sendOne(sv, user, recipient, message);
		return ;
	}
	std::vector<std::string>	recipients;
	splitParams(msg.params[0].value, recipients);
	for (size_t i = 0; i < recipients.size(); ++i) {
		try {
			sendOne(sv, user, recipients[i], message);
		} catch (const IRCError& e) {
			sv.sendTo(user, e.format(user.getNickname()));
		}
	}
}
