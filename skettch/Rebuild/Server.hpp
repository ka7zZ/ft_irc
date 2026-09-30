# pragma once

# include "User.hpp"
// # include "CommandDispatcher.hpp"
# include <string>
# include <map>
# include <vector>
# include <poll.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <fcntl.h>
# include <iostream>

class Server
{
	private:
		int			_port;
		int			_serverSocket;

		std::map<int, User>				_users;
		std::map<unsigned long, int>	_failedAttempts;
		std::vector<struct pollfd>		_pollSockets;
		// CommandDispatcher				_dispatcher;

		// SERVER RUN/CLOSE UTILS
		void	reconstructPollFd();
		void	processingEvents();
		void	closeServer();

		// SERVER INI UTILS
		bool	passCreateErrors();
		bool	passBindListenErrors(sockaddr_in* serverAddress);

		// SERVER INIT
		bool	initServer();

		// SERVER INTERACT
		int		handle_connections();
		void	registerClient();
		void	removeClient(int clientFd);

		// GETTING USERS
		User*	getClientBySocket(int clientSocket);
		User*	getClientByNickname(const std::string& nickname);

		// HANDLING RAW DATA
		void	handleClientData(int clientFd);

		// COMMUNICATION
		void	sendData(int targetFd, const std::string& message);
		void	sendMessageAll(int senderFd, const std::string& message);
		void	sendMessagePrivate(int senderFd, int receiverFd, const std::string& message);
		void	sendMessageChannel(int senderFd, const std::string& channelName, const std:::string& message);
		

	public:
		explicit Server(int port);
		~Server();

		void	run();
		void	stop();
};
