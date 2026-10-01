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
		int			_serverFd;

		std::map<int, User>				_users;
		std::vector<struct pollfd>		_pollSockets;
		// CommandDispatcher				_dispatcher;

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

		// GETTING USERS
		User*	getClientBySocket(int fd);
		User*	getClientByNickname(const std::string& nickname);

		// HANDLING RAW DATA
		void	handleClientData(int clientFd);

		// COMMUNICATION
		// void	sendData(int targetFd, const std::string& message);
		// void	sendMessageAll(int senderFd, const std::string& message);
		// void	sendMessagePrivate(int senderFd, int receiverFd, const std::string& message);
		// void	sendMessageChannel(int senderFd, const std::string& channelName, const std:::string& message);
		

	public:
		explicit Server(int port);
		~Server();

		void	run();
		void	stop();
};
