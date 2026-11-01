# include "../../headers/Commands/PartCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

void	PartCommand::splitParams(const std::string& list, std::vector<std::string>& out)
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

bool   PartCommand::needsRegistration() const { return true; }

size_t PartCommand::minParams() const { return 1; }

void   PartCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (msg.params[0].value.empty())
		throw IRCError(461, "PART");

	std::vector<std::string>	channels;
	splitParams(msg.params[0].value, channels);
	const std::string	reason = msg.params.size() > 1 ? msg.params[1].value : "";

	for (size_t i = 0; i < channels.size(); ++i)
	{
		try {
			Channel*	ch = sv.getChannel(channels[i]);
			if (!ch)
				throw IRCError(403, channels[i]);
			if (!ch->isMember(user.getSocket()))
				throw IRCError(442, channels[i]);
			sv.broadcast(*ch, ":" + user.prefix() + " PART " + ch->getName() + (reason.empty() ? "" : " :" + reason) + "\r\n", -1);
			ch->removeMember(user.getSocket()); 
		} catch (IRCError& e) {
			sv.sendTo(user, e.format(user.getNickname()));
		}
	}

}

