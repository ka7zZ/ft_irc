#pragma once

# include <map>
# include <string>
# include "Token.hpp"

enum TokenType
{
	PREFIX,
	COMMAND,
	PARAMETER,
	TRAILING,
	END_OF_LINE
};

class	Server;
class	User;

class	Dispatcher {

	public:
		typedef void	(*CommandHandler)(Server*, User* , std::map<TokenType, std::string>*);

	private :
		std::map<TokenType, std::string>		_lexer;
		std::map<std::string, CommandHandler>	_cmdHandler;
		Server*									_server;
		User*									_transmitter;


		// LEXER UTILS
		size_t	orEnd(size_t pos, size_t length);
		char	currentChar(const std::string& src, size_t index);
		size_t	skipWhitespaces(const std::string&src, size_t index);
		// LEXER TOKENIZER
		void	parseSrc(const std::string& src);

		// DEFINE COMMANDS
		void	handlePass(Server*, User* , std::map<TokenType, std::string>*);
		void	handleNickname(Server*, User* , std::map<TokenType, std::string>*);
		void	handleUsername(Server*, User* , std::map<TokenType, std::string>*);
		void	handleJoin(Server*, User* , std::map<TokenType, std::string>*);
		void	handlePrivate(Server*, User* , std::map<TokenType, std::string>*);
		void	handleKick(Server*, User* , std::map<TokenType, std::string>*);
		void	handleInvite(Server*, User* , std::map<TokenType, std::string>*);
		void	handleTopic(Server*, User* , std::map<TokenType, std::string>*);
		void	handleMode(Server*, User* , std::map<TokenType, std::string>*);

		"PASS",
		"NICK",
		"USER",
		"JOIN",
		"PRIVMSG",
		"KICK",
		"INVITE",
		"TOPIC",
		"MODE"

};