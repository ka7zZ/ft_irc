# include "../../headers/Commands/UserCommand.hpp"
# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Client.hpp"
# include "../../headers/Channel.hpp"
# include "../../headers/IRCError.hpp"

bool	UserCommand::needsRegistration() const { return false; }

size_t	UserCommand::minParams() const { return 4; }

void	UserCommand::execute(Server& sv, Client& user, const Message& msg) const
{
    if (!user.getNickState())
        throw IRCError(451);
    
}
