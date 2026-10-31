#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>

#include "IRCParser.hpp"
#include  <map>

class Bot {

	private:
		typedef void (Bot::*Handler)(const Message &);
		
		std::map<std::string, Handler> _serverHandlers;   // "001", "PRIVMSG"
		std::map<std::string, Handler> _userCommands;     // "!roll", "!help"
		
		std::string _hostname;
		int	_port;
		std::string _password;
		int bot_fd;
		struct sockaddr_in myaddr;

		std::string _nickname;
		std::string _username;
		std::string _realname;

		
	public:
		Bot(std::string, std::string);
		~Bot();
		

		void	login();
		void	prompt_BotInterface();
		void	initializeBot();
		void	connectBot();
		void	keepWaitingReply();
		void	handleLine(const Message &msg);
		

		//dispatcher
		void	initServerHandlers();
		void	initUserCommands();

		//Features
		void	onRoll(const Message& msg);
		void	onHelp(const Message& msg);
		void	onPrivmsg(const Message& msg);
		void	onWelcome(const Message& msg);
		
		// helper function
		void	sendMessage(const std::string& text);
};
