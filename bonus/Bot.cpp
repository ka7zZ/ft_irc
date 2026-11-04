# include "../headers/IRCParser.hpp"
# include "Bot.hpp"
# include "Dice.hpp"
# include <arpa/inet.h>
# include <climits>
# include <cstring>
# include <cstdlib>
# include <iostream>
# include <string>

extern volatile sig_atomic_t	g_running;

Bot::Bot(const std::string& hostname, const std::string& port)
:_hostname(hostname)
{
	_port = 0;
	char*	end = NULL;
	errno = 0;
	long	value = std::strtol(port.c_str(), &end, 10);
	if (end == hostname || value < INT_MIN || value > INT_MAX)
		throw std::runtime_error("port error");
	_port = static_cast<int>(value);

	if (initBot() == -1)
		throw std::runtime_error("socket failed");

	if (connectBot() == -1)
		throw std::runtime_error("connection failed");	

	_serverHandlers["001"] = &Bot::onWelcome;
	_serverHandlers["privmsg"] = &Bot::onPrivmsg;

	_userCommands["!roll"] = &Bot::onRoll;
	_userCommands["!help"] = &Bot::onHelp;
	prompt_BotInterface();
	login();
}

Bot::~Bot()
{
	_serverHandlers.clear();
	_userCommands.clear();
}

void	Bot::onWelcome(const Message& msg)
{
	(void)msg;
	std::string  reply = "JOIN #game\r\n";
	sendMessage(reply);
}

void	Bot::onRoll(const Message& msg)
{
    const std::string	&channel = msg.params[0].value;
	
	if (msg.params.size() < 2)
		return ;
	
	Dice roll(msg.params[1].value.substr(5), msg.prefix);
	if ((msg.params.size() == 2 && roll.getArgs() == "!roll" ) ||
		!roll.extractValues()) {
			sendMessage(msg.command + " " + channel + " :" + roll.invalidSyntax() + "\r\n");
			return ;
		}
		
	if (!roll.validateRangeValues()) {
		sendMessage(msg.command + " " + channel + " :" + roll.limitExceeded() + "\r\n");
		return ;
	}	
	roll.runDice();		
	roll.sumValues();
	sendMessage(msg.command + " " + channel + " :" + roll.resultDice() + "\r\n");
}

void	Bot::onHelp(const Message &msg)
{
	const std::string &channel = msg.params[0].value;
	std::string nick = msg.prefix.substr(0, msg.prefix.find('!'));

	std::string reply;
	reply += "PRIVMSG " + channel + " :" + nick + ": Dice bot commands:\r\n";
	reply += "PRIVMSG " + channel + " :!roll [N]d[M] - roll N dice with M sides, e.g. !roll 2d6 (1-10 dice, 2-20 sides)\r\n";
	reply += "PRIVMSG " + channel + " :!roll - roll one 20-sided die (1d20)\r\n";
	reply += "PRIVMSG " + channel + " :!help - show this help\r\n";
	
	sendMessage(reply);
}

void	Bot::onPrivmsg(const Message& msg)
{
	const std::string&	channel = msg.params[0].value;
	std::string			command;
	
	if (channel[0] != '#')
		return;
	
	size_t	end = msg.params[1].value.find(' ');
	if (end == std::string::npos)
		command = msg.params[1].value;
	else
		command = msg.params[1].value.substr(0, end);
	
	std::map<std::string, Handler>::iterator it = _userCommands.find(command); // if commands is "!roll"
	if (it != _userCommands.end())
		(this->*(it->second))(msg);
}

int	Bot::initBot()
{	
	_myaddr.sin_family = AF_INET;
	_myaddr.sin_port = htons(_port);
	_myaddr.sin_addr.s_addr = inet_addr(_hostname.c_str());
	memset(_myaddr.sin_zero, '\0', sizeof _myaddr.sin_zero);
	
	_botFd = socket(PF_INET, SOCK_STREAM, 0);
	return _botFd; 
}

int	Bot::connectBot() { return connect(_botFd, (struct sockaddr *)&_myaddr, sizeof(_myaddr)); }

void	Bot::prompt_BotInterface()
{
	std::cout << std::endl;
	std::cout << "  ==========================================" << std::endl;
	std::cout << "              42 BOT CLIENT" << std::endl;
	std::cout << "  ==========================================" << std::endl;
	std::cout << std::endl;
	std::cout << "  Welcome to the IRC Bot configuration!" << std::endl;
	std::cout << "  Please enter your connection details." << std::endl;
	std::cout << std::endl;
	std::cout << "  +----------------------------------------+" << std::endl;
	std::cout << "  | PASS     : ";
	std::getline(std::cin, _password);
	std::cout << "  | USERNAME : ";
	std::getline(std::cin, _username);
	std::cout << "  | REALNAME : ";
	std::getline(std::cin, _realname);
	std::cout << "  | NICKNAME : ";
	std::getline(std::cin, _nickname);
	std::cout << "  +----------------------------------------+" << std::endl;
	std::cout << std::endl;
	std::cout << "  Configuration completed!" << std::endl;
	std::cout << "  ==========================================" << std::endl;
	std::cout << std::endl;
}

void Bot::login()
{
	std::string msgToSend;
	
	msgToSend = "PASS ";
	msgToSend += _password;
	msgToSend += "\r\n";

	msgToSend += "NICK ";
	msgToSend += _nickname;
	msgToSend += "\r\n";

	msgToSend += "USER ";
	msgToSend += _username;
	msgToSend += " * 0 :";
	msgToSend += _realname;
	msgToSend += "\r\n";

	sendMessage(msgToSend);
}

void	Bot::sendMessage(const std::string& text)
{	
	int ret_send = send(_botFd, text.c_str(), text.length(), 0);
	
	if (ret_send == -1)
		throw std::runtime_error("send failed");
}

void	Bot::handleLine(const Message &msg)
{
	std::map<std::string, Handler>::iterator it = _serverHandlers.find(msg.command);
	if (it != _serverHandlers.end())
		(this->*(it->second))(msg);	
}

void	Bot::run()
{
	char buffer[512];
	std::string tmp_buffer;
	memset(buffer, 0, sizeof(buffer));
	while (g_running) 
	{
		int ret_recv = recv(_botFd, buffer, sizeof(buffer), 0);
		if (ret_recv == -1)
			throw std::runtime_error("recv failed");
		if (ret_recv == 0)
		{
			std::cout << "server closed the connection" << std::endl;
			break;
		}
		tmp_buffer.append(buffer, ret_recv);
		size_t pos;
		while ((pos = tmp_buffer.find("\r\n")) != std::string::npos)
		{
			std::string raw_line = tmp_buffer.substr(0, pos);
			tmp_buffer.erase(0, pos + 2);
			std::cout << raw_line << "\n";
			IRCParser parser(raw_line);
			Message msg = parser.parseTokens();
			handleLine(msg);
		}
	}
}
