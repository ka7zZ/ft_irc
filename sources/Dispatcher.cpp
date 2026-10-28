# include "../headers/Dispatcher.hpp"
# include "../headers/Commands/InviteCommand.hpp"
# include "../headers/Commands/JoinCommand.hpp"
# include "../headers/Commands/PassCommand.hpp"
# include "../headers/Commands/UserCommand.hpp"
# include "../headers/Commands/NickCommand.hpp"
# include "../headers/Commands/PrivmsgCommand.hpp"
# include "../headers/Commands/PingCommand.hpp"
# include "../headers/Commands/KickCommand.hpp"
# include "../headers/Commands/ModeCommand.hpp"
# include "../headers/Commands/TopicCommand.hpp"
# include "../headers/Commands/ListCommand.hpp"
# include "../headers/Server.hpp"
# include "../headers/IRCParser.hpp"
# include "../headers/Client.hpp"
# include "../headers/Channel.hpp"
# include "../headers/IRCError.hpp"
# include <cctype>

Dispatcher::Dispatcher() {
	try {
		_commands["pass"] = new PassCommand();
		_commands["nick"] = new NickCommand();
		_commands["user"] = new UserCommand();
		_commands["join"] = new JoinCommand();
        _commands["ping"] = new PingCommand();
		_commands["invite"] = new InviteCommand();
        _commands["privmsg"] = new PrivmsgCommand();
        _commands["kick"] = new KickCommand();
        _commands["mode"] = new ModeCommand();
        _commands["topic"] = new TopicCommand();
        _commands["list"] = new ListCommand();
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
		std::string command = msg.command;
		for (size_t i = 0; i < command.size(); ++i)
			command[i] = static_cast<char>(
				std::tolower(static_cast<unsigned char>(command[i])));
		std::map<std::string, ICommand*>::const_iterator it = _commands.find(command);
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