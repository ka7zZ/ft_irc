# include "../headers/Dispatcher.hpp"
# include "../headers/Commands/InviteCommand.hpp"
# include "../headers/Commands/JoinCommand.hpp"
# include "../headers/Commands/PassCommand.hpp"
# include "../headers/Commands/UserCommand.hpp"
# include "../headers/Commands/NickCommand.hpp"
# include "../headers/Server.hpp"
# include "../headers/IRCParser.hpp"
# include "../headers/Client.hpp"
# include "../headers/Channel.hpp"
# include "../headers/IRCError.hpp"

Dispatcher::Dispatcher() {
	try {
		_commands["PASS"] = new PassCommand();
		_commands["NICK"] = new NickCommand();
		_commands["USER"] = new UserCommand();
		_commands["JOIN"] = new JoinCommand();
		_commands["INVITE"] = new InviteCommand();
	} catch (std::exception& e) {
		clear();
		std::cout << e.what() << std::endl;
	}
}

Dispatcher::~Dispatcher() {
	clear();
}

void	Dispatcher::clear()
{
	std::map<std::string, ICommand*>::iterator   it;

	for (it = _commands.begin(); it != _commands.end(); ++it) {
		delete it->second;
	}
	_commands.clear();
}

void    Dispatcher::dispatch(Server& sv, Client& cl, const Message& msg)
{
	try {
		std::map<std::string, ICommand*>::const_iterator it = _commands.find(msg.command);
		if (it == _commands.end())
			throw IRCError(421, msg.command);
		
		ICommand* cmd = it->second;
		if (cmd->needsRegistration() && !cl.getAuthState())
			throw IRCError(451);
		if (cmd->minParams() > msg.params.size())
			throw IRCError(461, msg.command);
		cmd->execute(sv, cl, msg);
	} catch (IRCError& e) {
		sv.sendTo(cl, e.format(cl.getNickname()));
	}
}