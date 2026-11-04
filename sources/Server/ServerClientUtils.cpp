# include "../../headers/Server.hpp"
# include <arpa/inet.h>
# include <fcntl.h>
# include <unistd.h>

void	Server::registerClient()
{
		struct sockaddr_in	clientAddress;
	socklen_t			clientLength = sizeof(clientAddress);
	int					clientFd;
	unsigned short		clientPort;

	clientFd = accept(_serverFd, (struct sockaddr*)&clientAddress, &clientLength);
	if (clientFd < 0)
	{
		std::cout << "ERROR: Client socket." << std::endl;
		return ;
	}
	clientPort = ntohs(clientAddress.sin_port);
	const std::string   host = inet_ntoa(clientAddress.sin_addr);
	std::cout << "[Attempt to connect] " << host << ":" << clientPort << std::endl;
	if (clientFd < 0)
		return ;
	if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
		std::cout << "ERR: Unable to set non-blocking client socket" << std::endl;
		close(clientFd);
		return ;
	}
	_clients[clientFd] = Client(clientFd, host);
	std::cout << "[SOCKET #" << clientFd << "] Successfully connected." << std::endl;
}

void	Server::removeClient(int clientFd)
{
	std::map<int, Client>::iterator	itClient = _clients.find(clientFd);
	
	if (itClient == _clients.end()) 
		return ;
	std::map<std::string, Channel>::iterator  itChannel;
	for (itChannel = _channels.begin(); itChannel != _channels.end(); )
	{
		itChannel->second.removeInvited(clientFd);
		if (itChannel->second.isMember(clientFd))
		{
			itChannel->second.removeMember(clientFd);
			if (itChannel->second.getChannelSize() > 0)
				broadcast(itChannel->second, ":" + itClient->second.prefix() + " QUIT :Client disconnected.\r\n", -1);
		}
		if (itChannel->second.getChannelSize() == 0)
		{
			std::map<std::string, Channel>::iterator eraseBuffer = itChannel;
			++itChannel;
			_channels.erase(eraseBuffer);
		}
		else
			++itChannel;
	}
	_clients.erase(itClient);
	close(clientFd);
}


void	Server::disconnectClient(int clientFd, const std::string& reason)
{
	std::cout << "[SOCKET #" << clientFd << "] Disconnected: "
		<< reason << std::endl;
	removeClient(clientFd);
}

Client*	Server::getClientBySocket(int fd)
{
	std::map<int, Client>::iterator it = _clients.find(fd);

	if (it == _clients.end())
		return NULL;
	return &it->second;
}

Client*	Server::getClientByNickname(const std::string& nickname)
{
	std::map<int, Client>::iterator	it;
	const std::string	normalized = normalizeString(nickname);

	for (it = _clients.begin(); it != _clients.end(); it++) {
		if (normalizeString(it->second.getNickname()) == normalized) {
			return &it->second;
		}
	}
	return NULL;
}

Client* Server::getClientByPrefix(const std::string& prefix)
{
	std::map<int, Client>::iterator	it;

	for (it = _clients.begin(); it != _clients.end(); it++) {
		if (it->second.prefix() == prefix) {
			return &it->second;
		}
	}
	return NULL;
}

