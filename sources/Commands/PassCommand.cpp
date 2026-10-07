# include "../../headers/Commands/PassCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

bool	PassCommand::needsRegistration() const { return false; }

size_t	PassCommand::minParams() const { return 1; }

void 	PassCommand::execute(Server& sv, Client& user, const Message& msg) const
{
	if (user.getPassState())
		throw IRCError(462, user.getNickname());
    if (msg.params[0].value.empty())
		throw IRCError(461);
	if (msg.params[0].value != sv.getPassword()) {
		user.setMarkForClose(true);
		throw IRCError(464);
	}
	user.setPassState(true);
	sv.sendTo(user, "Password approved.");
}