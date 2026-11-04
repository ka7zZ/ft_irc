#ifndef BOT_HPP
# define BOT_HPP

#include <netinet/in.h>
#include <string>
#include <map>
#include <csignal>

extern volatile sig_atomic_t g_running;

struct Message;

class Bot {

	private:
		const std::string	_hostname;
		std::string 		_nickname;
		std::string 		_username;
		std::string 		_realname;
		int					_port;
		std::string 		_password;
		int 				_botFd;
		struct sockaddr_in	_myaddr;
		
		typedef void (Bot::*Handler)(const Message &);
		
		std::map<std::string, Handler> _serverHandlers;
		std::map<std::string, Handler> _userCommands;

		void	onRoll(const Message& msg);
		void	onHelp(const Message& msg);
		void	onPrivmsg(const Message& msg);
		void	onWelcome(const Message& msg);
		
		int		initBot();
		int		connectBot();
		
		void	prompt_BotInterface();
		void	login();

		void	handleLine(const Message &msg);
		void	sendMessage(const std::string& text);
		
	public:
		Bot(const std::string&, const std::string&);
		~Bot();

		void	run();
};

#endif