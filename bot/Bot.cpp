#include "Bot.hpp"
#include "IRCParser.hpp"
#include "dice.hpp"

void	Bot::initServerHandlers()
{
	_serverHandlers.clear();
	
	_serverHandlers["001"] = &Bot::onWelcome;
	_serverHandlers["PRIVMSG"] = &Bot::onPrivmsg;
//	_serverHandlers["PING"] = &Bot::onPing;
	
	if (_serverHandlers.size() != 2)
		throw std::runtime_error("Invalid server handler initialization");	
}

void	Bot::initUserCommands()
{
	_userCommands.clear();
	
	_userCommands["!roll"] = &Bot::onRoll;
	//_userCommands["!roll "] = &Bot::onRoll;
	_userCommands["!help"] = &Bot::onHelp;
	
	if (_userCommands.size() != 2)
		throw std::runtime_error("Invalid server handler initialization");	

}


void	Bot::sendMessage(const std::string& text)
{	
	int ret_send = send(bot_fd, text.c_str(), text.length(), 0);
	if (ret_send == -1)
		throw std::runtime_error("send failed");
}

void	Bot::onWelcome(const Message& msg)
{
	std::string  reply = "JOIN #game\r\n";
	sendMessage(reply);
}


void	Bot::onRoll(const Message& msg)
{

    const std::string &channel = msg.params[0].value;
	std::string arg;
	if (msg.params[1].value == "!roll")
		arg = "";
	else
		arg = msg.params[1].value.substr(5);

	Dice roll(arg, msg.prefix); 
	std::string reply;
	if (!roll.extractValues())
		reply = roll.usageDice();
	else if (!roll.validateRangeValues())
		reply = roll.limitsDice();
	else
	{
		roll.runDice();		
		roll.sumValues();
		reply = roll.resultDice();
	}
	reply = msg.command + " " + channel + " :"+ reply + "\r\n";
	std::cout << "myreply ==> " << reply ;
	sendMessage(reply);
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

	std::string channel = msg.params[0].value;
	std::string command  = msg.params[1].value;
	
	if (channel[0] != '#')
		return;
	
	command = command.substr(0, command.find(' '));
	std::map<std::string, Handler>::iterator it = _userCommands.find(command); // if commands is "!roll"
	if (it != _userCommands.end())
		(this->*(it->second))(msg);

}


Bot::Bot(std::string hostname, std::string  port)
:_hostname(hostname)
{
	_port = atoi(port.c_str());
}

Bot::~Bot()
{}


void Bot::initializeBot()
{
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(_port);
	myaddr.sin_addr.s_addr = inet_addr(_hostname.c_str());
	memset(myaddr.sin_zero, '\0', sizeof myaddr.sin_zero);
	
	bot_fd = socket(PF_INET, SOCK_STREAM, 0);
	
	if (bot_fd == -1)
		throw std::runtime_error("socket failed");	
	std::cout << "bot is initialized\n";
}

void	Bot::connectBot()
{
	int ret_connect = connect(bot_fd, (struct sockaddr *)&myaddr, sizeof(myaddr));

	if (ret_connect == -1)
		throw std::runtime_error("connect failed");	
	std::cout << "bot start the  connection\n";
}

void	Bot::handleLine(const Message &msg)
{
	std::map<std::string, Handler>::iterator it = _serverHandlers.find(msg.command);
	if (it != _serverHandlers.end())
		(this->*(it->second))(msg);	
}

void	Bot::keepWaitingReply()
{
	char buffer[512];
	std::string tmp_buffer;
	memset(buffer, 0, sizeof(buffer));

	while (true) 
	{
		
		int ret_recv = recv(bot_fd, buffer, sizeof(buffer), 0);
		if (ret_recv == -1)
			throw std::runtime_error("recv failed");
		if (ret_recv == 0)
		{
			std::cout << "server closed the connection" << std::endl;
			break;
		}
		tmp_buffer.append(buffer, ret_recv);
		size_t pos;
		while (  (pos = tmp_buffer.find("\r\n")) != std::string::npos)
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
