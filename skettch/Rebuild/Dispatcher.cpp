#include "Dispatcher.hpp"
#include "Server.hpp"
#include "User.hpp"
#include <iostream>

Dispatcher::Dispatcher(const std::string& rawData) {
	parseSrc(rawData);
}

Dispatcher::~Dispatcher() {
	_lexer.clear();
}

//	LEXER UTILS
size_t	Dispatcher::orEnd(size_t pos, size_t length) {
	if (pos == std::string::npos)
		return length;
	return pos;
}

//	LEXER UTILS
char	Dispatcher::currentChar(const std::string& src, size_t index) {
	if (index >= src.length())
		return '\0';
	return src[index];
}

//	LEXER UTILS
size_t	Dispatcher::skipWhitespaces(const std::string& src, size_t index) {

    index = src.find_first_not_of(' ', index);
	
    if (index == std::string::npos)
		index = src.length();
	return index;
}

// LEXER PARSER
void	Dispatcher::parseSrc(const std::string& src) {
	size_t	index = skipWhitespaces(src, 0);
	size_t	length = src.length();
	size_t	end = 0;

	if (index < length && currentChar(src, index) == ':') {
		index += 1;
		end = orEnd(src.find_first_of(" ", index), length);
		if (end > index)
			_lexer.insert(std::make_pair(PREFIX, src.substr(index, end - index)));
		index = skipWhitespaces(src, end);
	}
	if (index < length) {
		end = orEnd(src.find_first_of(" ", index), length);
		if (end > index)
			_lexer.insert(std::make_pair(COMMAND, src.substr(index, end - index).));
		index = end;
	}
	while (index < length) {
		index = skipWhitespaces(src, index);
		if (index >= length)
			break ;
		if (currentChar(src, index) == ':') {
			_lexer.insert(std::make_pair(TRAILING, src.substr(index + 1)));
			break ;
		}
		end = orEnd(src.find_first_of(" ", index), length);
		if (end > index)
			_lexer.insert(std::make_pair(PARAMETER, src.substr(index, end - index)));
		index = end;
	}
	_lexer.insert(std::make_pair(END_OF_LINE, src.substr(index, 0)));
}

// DEFINE CMD PASS
void	Dispatcher::handlePass(Server* sv, User* usr , std::map<TokenType, std::string>* line)
{
	std::map<TokenType, std::string>::iterator	it = line->begin();

	if (usr->getAuthState()) {
		std::cout << "462: ERR_ALREADYREGISTRED." << std::endl;
		return ;
	}
	it = line->begin();
	if ((++it)->second.empty()) {
		std::cout << "461: ERR_NEEDMOREPARAMS." << std::endl;
		return ;
	}
	if ((it)->second == sv->getPassword()) {
		usr->setPassState(true);
		std::cout << "Password approved. Now you can register with your username and a nickname." << std::endl;
	} else {
		std::cout << "464: ERR_PASSWDMISMATCH." << std::endl;
		sv->setDisconnect(true);
	}
}

// DEFINE CMD SET NICKNAME
void	Dispatcher::handleNickname(Server* sv, User* usr , std::map<TokenType, std::string>* line) {
	std::map<TokenType, std::string>::iterator	it = line->begin();
	
	if (usr->getPassState() == true) {
		it = line->begin();
		if ((++it) != line->end() && it->second.empty()) {
			std::cout << "431: ERR_NONICKNAMEGIVEN." << std::endl;
			return ;
		}
		if (sv->getClientByNickname(it->second)) {
			std::cout << "433 ERR_NICKNAMEINUSE" << std::endl;
			return ;
		}
		usr->setNickname(it->second);
		if (usr->getUserState()) {
			usr->setUserState(true);
			std::cout << "Successfully registered." << std::endl;
			return ;
		}
		usr->setNickState(true);
		std::cout << "Nickname set." << std::endl;
	}
	else
		std::cout << "451: PASS <password> needed." << std::endl;
}

// DEFINE CMD SET USERNAME
void	Dispatcher::handleUsername(Server* sv, User* usr , std::map<TokenType, std::string>* line) {
	std::map<TokenType, std::string>::iterator	it;
	
	if (usr->passOk == true) {
		it = line->begin();
		if ((++it) && it->second.empty()) {
			std::cout << "461: Insert a valid username." << std::endl;
			return ;
		}
		usr->setUsername() = it->second;
		if (usr->isNamed()) {
			usr->setAuth(true);
			std::cout << "Successfully registered." << std::endl;
			return ;
		}
		usr->setNamed(true);
		std::cout << "Username set." << std::endl;
	}
	else
		std::cout << "451: PASS <password> needed." << std::endl;
}

// DEFINE CMD JOIN
void	Dispatcher::handleJoin(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;
	const char*									channelName;
	size_t										skipIndex;

	if (usr->isAuth) {
		for (it = line->begin(); it != line->end(); ++it) {
			if (it == line->end()) {
				std::cout << "461: ERR_NEEDMOREPARAMS." << std::endl;
				return ;
			}
			// SKIP ':' CHAR -> SHOULD BE IGNORED
			// HAVE TOO IMPLEMENT DISPATCH UTIL FUNCTION
			if (checkChannelFormat(it, it->second)) {
				skipIndex = returnIndexName(it->second);
				channelName = it->second.c_str() + skipIndex;
				if (!passChannelErrors(channelName, userNickname))
					return ;
				if (!channelName) {
					std::cout << "461: ERR_NEEDMOREPARAMS." << std::endl;
				}
				// HAVE TO IMPLEMENT DISPATCH UTIL FUNCTION
				if (!getChannel(channelName))
					return ;
				// HAVE TO IMPLEMENT SERVER UTIL FUNCTION
				if (sv.getUserFromChannel(usr->getNickname())) {
					std::cout << "User: " << usr->getNickname() << ": already exists." << std:endl;
					return ;
				}
				// HAVE TO IMPLEMENT CHANNEL CLASS
				sv._channels.add(usr->getNickname());
				return ;
			}
			std::cout << ""
		}
	} else
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}

// DEFINE CMD PRIVATE MESSAGE
void	Dispatcher::handlePrivate(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;
	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}

// DEFINE CMD KIK
void	Dispatcher::handleKick(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}

// DEFINE CMD INVITE
void	Dispatcher::handleInvite(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}

// DEFINE CMD TOPIC
void	Dispatcher::handleTopic(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}

// DEFINE CMD MODE
void	Dispatcher::handleMode(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	std::map<TokenType, std::string>::iterator	it;
	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;
	
}
