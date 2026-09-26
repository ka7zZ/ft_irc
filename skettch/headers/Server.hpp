#ifndef SERVER_HPP
# define SERVER_HPP

# include <string>
# include <map>
# include <vector>
# include <poll.h>
# include "User.hpp"
# include "CommandDispatcher.hpp"

class Server
{
	private:
		int			_port;
		bool		_isRunning;
		int			_serverFd;

		std::map<int, User*>		_users;
		std::vector<struct pollfd>	_pollFds;
		CommandDispatcher           _dispatcher;

		bool	initServer();
		void	acceptClient();
		void	handleClientData(int clientFd);
		void	removeClient(int clientFd);

	public:
		explicit Server(int port);
		~Server();

		void	start();
		void	stop();
		void	addUser(User* user);
		void	removeUser(int id);
		User*	getUserByNick(const std::string& nickname);
		bool	isRunning() const;
};

#endif
