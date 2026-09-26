# include "../headers/CommandDispatcher.hpp"
#include "../headers/Server.hpp"
#include "../headers/User.hpp"
#include <iostream>

CommandDispatcher::CommandDispatcher()
{
	_handlers["NICK"] = &CommandDispatcher::handleNick;
	_handlers["USER"] = &CommandDispatcher::handleUser;
	_handlers["PING"] = &CommandDispatcher::handlePing;
	_handlers["PRIVMSG"] = &CommandDispatcher::handlePrivMsg;
}

CommandDispatcher::~CommandDispatcher() {}

void    CommandDispatcher::dispatch(Server& server, User& user, const MessageAST& ast)
{
	std::map<std::string, CommandHandler>::iterator it = _handlers.find(ast.command);

	if (it != _handlers.end())
	{
		it->second(server, user, ast);
	}
	else
	{
		std::cout << "[CMD HANDLER] Command: " << ast.command << "; Not Found" << std::endl;
	}
}

void CommandDispatcher::handleNick(Server& server, User& user, const MessageAST& ast)
{
	if (ast.params.empty())
		return ;
	std::string newNick = ast.params[0]->value;
	user.setNickname(newNick);
	std::cout << "Nickname changed successfully." << std::endl;
}

void CommandDispatcher::handlePing(Server& server, User& user, const MessageAST& ast)
{
	if (ast.params.empty())
		return ;
	std::string reply = "PONG: " + ast.params[0]->value + "\r\n";
	user.getSocket()->send(reply.c_str(), reply.length());
}

void CommandDispatcher::handleUser(Server& server, User& user, const MessageAST& ast)
{
	if (ast.params.empty())
        return ;
    user.setUsername(ast.params[0]->value);
}

void CommandDispatcher::handlePrivMsg(Server& server, User& user, const MessageAST& ast)
{
	// VALIDATE NUMBER OF PARAMETERS (at least 2: whom and text)
	if (ast.params.size() < 2)
	{
		std::cout << "[PRIVATE MESSAGE] Invalid number parameters." << std::cout;
		return ;
	}

	// DATA EXTRACT
	std::string target = ast.params[0]->value;
	std::string msg = ast.params[1]->value;

	std::cout << "[" << user.getNickname() << "] sends to [" << target << "]" << std::endl;

	if (!target.empty() && target[0] == '#')
	{
		// IMPLEMENT CHANNEL CLASS
		std::cout << "This will go to the channel" << std::endl;
	}
	else
	{
		User* targetUser = server.getUserByNick(target);
		if (targetUser != NULL)
		{
			std::string fullMessage = "[PRIVATE MESSAGE from (" + user.getNickname() + ")]: " + msg + "\r\n";
			targetUser->getSocket()->send(fullMessage.c_str(), fullMessage.length());
		}
		else
		{
			std::cout << "ERROR: Nickname unavailable." << std::endl;
		}
	}
}
