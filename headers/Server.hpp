#ifndef SERVER_HPP
# define SERVER_HPP

// # include "CommandDispatcher.hpp"
# include "Client.hpp"
# include "Channel.hpp"
# include "Dispatcher.hpp"
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

		std::map<int, Client>				_clients;	
		std::vector<struct pollfd>			_pollSockets;
		std::map<std::string, Client>		_userPasswords;
		std::vector<Channel>                _channels;
		Dispatcher							_dispatcher;

		// OCF
		Server(const Server&);
		Server& operator=(const Server&);

		// SERVER UTILS
		void	disconnectClient(int fd, const std::string& reason);
		void	rebuildPollFds();
		void	handlePollEvents();
		void	shutdownServer();
		bool	exceedsPendingLimit(size_t bytesRead, size_t maxBytes);
		bool	extractValidData(Client *target);
		bool	passCreateErrors();
		bool	passBindListenErrors(sockaddr_in* serverAddress);

		// SERVER INIT
		bool	initServer();

		// SERVER INTERACT
		void	registerClient();
		void	removeClient(int clientFd);

		// HANDLING RAW DATA
		void	handleClientData(int clientFd);

		// GETTING USERS
		Client*	getClientBySocket(int fd);
		

		// COMMUNICATION
		
		void	sendData(int targetFd);

	public:
		explicit Server(int port, const std::string& password);
		~Server();

		void				run();
		void				stop();

		Channel*			getChannel(const std::string& channelName);
		const std::vector<Channel>&	getChannels() const;
		const std::string&	getPassword() const;
		Client*				getClientByNickname(const std::string& nickname);
		Client* 			getClientByPrefix(const std::string& prefix);
		void				setPassword(const std::string& password);

		bool				isChannel(const std::string& name) const;
		bool				isClient(const std::string& name) const;
		bool				isClientInChannel(Channel* ch, Client* cl);
		
		void				createChannel(Client* cl, const std::string& name);
		
		void				sendTo(Client& cl, const std::string& text);
		void				broadcast(const Channel& ch, const std::string& msg, int exceptFd);
		void				sendPrivateMessage(int senderFd, int receiverFd, const std::string& message);
		void				sendChannelMessage(int senderFd, const std::string& channelName, const std::string& message);
};

#endif