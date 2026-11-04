#ifndef SERVER_HPP
# define SERVER_HPP

# include "Client.hpp"
# include "Channel.hpp"
# include "Dispatcher.hpp"
# include <map>
# include <vector>
# include <string>
# include <poll.h>
# include <netinet/in.h>

class Server
{
	private:
		int			_port;
		std::string _password;
		int			_serverFd;
		ssize_t		_counter;

		std::map<int, Client>			_clients;	
		std::vector<struct pollfd>		_pollSockets;
		std::map<std::string, Client>	_userPasswords;
		std::map<std::string, Channel>	_channels;
		Dispatcher						_dispatcher;

		Server(const Server&);
		Server& operator=(const Server&);

		bool									initServer();
		void									shutdownServer();
		std::string								normalizeString(const std::string& param) const;
		void									refreshReceivedFlag();

		bool									passCreateErrors();
		bool									passBindListenErrors(sockaddr_in* serverAddress);
		void									sendData(int targetFd);
		void									rebuildPollFds();
		void									handlePollEvents();

		bool									exceedsPendingLimit(size_t bytesRead, size_t maxBytes);
		bool									parseRawData(Client *target);
		void									handleRawData(int clientFd);

		void									registerClient();
		void									removeClient(int clientFd);
		void									disconnectClient(int fd, const std::string& reason);
		Client*									getClientBySocket(int fd);
		
	public:
		explicit Server(int port, const std::string& password);
		~Server();

		void									run();
		void									stop();

		const std::string&						getPassword() const;
		void									setPassword(const std::string& password);

		void									sendTo(Client& cl, const std::string& text);
		void									broadcast(const Channel& ch, const std::string& msg, int exceptFd);
		void									broadcastAll(Client* cl, const std::string& msg);
		
		Client*									getClientByNickname(const std::string& nickname);
		Client* 								getClientByPrefix(const std::string& prefix);
		
		void									createChannel(Client* cl, const std::string& name);
		void									removeChannel(Channel *ch);
		Channel*								getChannel(const std::string& channelName);
		const std::map<std::string, Channel>&	getAllChannels() const;

		
		
};

#endif