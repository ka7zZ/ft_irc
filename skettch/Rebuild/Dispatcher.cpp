#include "Dispatcher.hpp"
#include "Server.hpp"
#include "User.hpp"

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
	if (index == std::string:npos)
		index = src.length();
	return index;
}

// LEXER PARSER
void	Dispatcher::parseSrc(const std::string& src) {
	size_t	index = src.find_first_not_of(' ', 0);
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
			_lexer.insert(std::make_pair(COMMAND, src.substr(index, end - index)));
		index = end;
	}
	while (index < length) {
		index = skipWhitespaces(src, index);
		if (index >= length)
			break ;
		if (currentChar(src, index) == ':') {
			_lexer.insert(std::make_pair(TRAILING, src.substr(index, length)));
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
void	handlePass(Server* sv, User* usr , std::map<TokenType, std::string>* line)
{
	std::map<TokenType, std::string>::iterator	it;

	if (usr->isAuth) {
		std::cout << "462: ERR_ALREADYREGISTRED." << std::endl;
		return ;
	}
	if (!(++it)->second) {
		std::cout << "461: ERR_NEEDMOREPARAMS." << std::endl;
		return ;
	}
	if ((it)->second == sv->getPassword()) {
		usr->setPassOk(true);
		std::cout << "Password approved. Now you can register with your username and a nickname." << std::endl;
	} else {
		std::cout << "464: ERR_PASSWDMISMATCH." << std::endl;
		sv->setDisconnect(true);
	}
}

// DEFINE CMD SET NICKNAME
void	handleNickname(Server* sv, User* usr , std::map<TokenType, std::string>* line) {
	std::map<TokenType, std::string>::iterator	it;
	
	if (usr->passOk == true) {
		if ((++it) && it->second.empty()) {
			std::cout << "431: ERR_NONICKNAMEGIVEN." << std::endl;
			return ;
		}
		if (sv->getClientByNickname(it->second)) {
			std::cout << "433 ERR_NICKNAMEINUSE" << std::endl;
			return ;
		}
		usr->setNickname = it->second;
		if (usr->isNamed()) {
			usr->setAuth(true);
			std::cout << "Successfully registered." << std::endl;
			return ;
		}
		usr->setNamed(true);
		std::cout << "Nickname set." << std::endl;
	}
	else
		std::cout << "451: PASS <password> needed." << std::endl;
}

// DEFINE CMD SET USERNAME
void	handleUsername(Server* sv, User* usr , std::map<TokenType, std::string>* line) {
	std::map<TokenType, std::string>::iterator	it;
	
	if (usr->passOk == true) {
		if ((++it) && it->second.empty()) {
			std::cout << "461: Insert a valid username." << std::endl;
			return ;
		}
		usr->setUsername = it->second;
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
void	handleJoin(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}
// DEFINE CMD PRIVATE MESSAGE
void	handlePrivate(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}
// DEFINE CMD KIK
void	handleKick(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}
// DEFINE CMD INVITE
void	handleInvite(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}
// DEFINE CMD TOPIC
void	handleTopic(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;

}
// DEFINE CMD MODE
void	handleMode(Server* sv, User* usr , std::map<TokenType, std::string>* line) {

	if (usr->isAuth) {
	} else 
		std::cout << "451: ERR_NOTREGISTERED." << std::endl;
	
}
