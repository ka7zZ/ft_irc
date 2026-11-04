# include "../../headers/Server.hpp"
# include <cstring>			// memset
# include <unistd.h>		// close fds

bool	Server::initServer()
{
	// CREATE SERVER SOCKET
	_serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (!passCreateErrors())
		return false;
	
	// BINDING AND LISTENING
	struct sockaddr_in	serverAddress;
	std::memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(_port);

	if (!passBindListenErrors(&serverAddress))
		return false;
	return true;
}

void	Server::shutdownServer()
{
	std::map<int, Client>::iterator	it;

	for (it = _clients.begin(); it != _clients.end(); ++it) {
		close(it->first);
	}
	_clients.clear();
	_pollSockets.clear();
	std::cout << "Successfully closed all sockets." << std::endl;
	if (_serverFd != -1) {
		close(_serverFd);
		_serverFd = -1;
	}
	std::cout << "Server closed." << std::endl;
}

std::string	Server::normalizeString(const std::string& param) const
{
	std::string	normalized = param;

	for (size_t i = 0; i < normalized.size(); ++i)
	{
		if (normalized[i] >= 'A' && normalized[i] <= 'Z')
			normalized[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(normalized[i])));
	}
	return normalized;
}

void	Server::refreshReceivedFlag()
{
	std::map<std::string, Channel>::iterator	it = _channels.begin();

	for (;it != _channels.end(); ++it) {
		std::map<int, Member>&	members = it->second.getMembers();
		std::map<int, Member>::iterator	membIt;
		for (membIt = members.begin(); membIt != members.end(); ++membIt)
		{
			if (membIt->second.cl->isReceived())
				membIt->second.cl->setReceived(false);
		}
	}
}

void	Server::broadcast(const Channel& ch, const std::string& msg, int exceptFd)
{
	const std::map<int, Member>&			members = ch.getAllMembers();
	std::map<int, Member>::const_iterator	it;

	for (it = members.begin(); it != members.end(); ++it)
	{
		if (exceptFd > 0 && it->first == exceptFd)
			continue ;
		if (!it->second.cl->isReceived())
			sendTo(*it->second.cl, msg);
	}
}

void	Server::broadcastAll(Client* cl, const std::string& msg)
{
	std::map<std::string, Channel>::iterator	it = _channels.begin();
	
	for (;it != _channels.end(); ++it)
	{
		if (it->second.isMember(cl->getSocket())) 
		{
			broadcast(it->second, msg, -1);
			std::map<int, Member>&	members = it->second.getMembers();
			std::map<int, Member>::iterator	membIt;
			for (membIt = members.begin(); membIt != members.end(); ++membIt)
			{
				if (!membIt->second.cl->isReceived())
					membIt->second.cl->setReceived(true);
			}
		}
	}
	refreshReceivedFlag();
}
