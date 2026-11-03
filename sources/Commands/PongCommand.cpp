# include "../../headers/Commands/PongCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/IRCError.hpp"

bool	PongCommand::needsRegistration() const { return true; }

size_t	PongCommand::minParams() const { return 0; }

void	PongCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    (void) sv;
    (void) user;
	if (msg.params.empty())
		throw IRCError(409);
}