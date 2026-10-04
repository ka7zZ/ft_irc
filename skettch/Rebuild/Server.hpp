# pragma once
// # include "CommandDispatcher.hpp"
# include "User.hpp"
# include <map>				// containers
# include <vector>			// containers
# include <string>			// std::string
# include <poll.h>			// poll struct
# include <netinet/in.h>	// sockaddr_in



class Server
{
	private:
		int			_port;
		std::string _password;
		int			_serverFd;
		bool		_disconnectCurrentUser;

		std::map<int, User>				_users;
		std::map<std::string, User>     _userPasswords;
		std::vector<struct pollfd>		_pollSockets;
		// Dispatcher				    _dispatcher;

		// OCF
		Server(const Server&);
		Server& operator=(const Server&);

		// SERVER UTILS
		void	disconnectClient(int fd, const std::string& reason);
		void	rebuildPollFds();
		void	handlePollEvents();
		void	shutdownServer();
		bool	exceedsPendingLimit(size_t bytesRead, size_t maxBytes);
		bool	extractValidData(User *target);
		bool	passCreateErrors();
		bool	passBindListenErrors(sockaddr_in* serverAddress);

		// SERVER INIT
		bool	initServer();

		// SERVER INTERACT
		int		handle_connections();
		void	registerClient();
		void	removeClient(int clientFd);

		// HANDLING RAW DATA
		void	handleClientData(int clientFd);

		// GETTING USERS
		User*	getClientBySocket(int fd);
		

		// COMMUNICATION
		// void	sendData(int targetFd, const std::string& message);
		// void	sendMessageAll(int senderFd, const std::string& message);
		// void	sendMessagePrivate(int senderFd, int receiverFd, const std::string& message);
		// void	sendMessageChannel(int senderFd, const std::string& channelName, const std:::string& message);
		

	public:
		explicit Server(int port, const std::string& password);
		~Server();

		void				run();
		void				stop();

		const std::string&	getPassword() const;
		bool				getDisconnect();
		User*				getClientByNickname(const std::string& nickname);

		void				setPassword(const std::string& password);
		void				setDisconnect(bool disconnectCurrentUser);
};
