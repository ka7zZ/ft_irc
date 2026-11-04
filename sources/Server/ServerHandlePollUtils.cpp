# include "../../headers/Server.hpp"
# include <fcntl.h>

bool	Server::passCreateErrors()
{
	if (_serverFd < 0) {
		std::cout << "ERROR: Server socket assigning failure." << std::endl;
		return false;
	}
	// SETTING SOCKET OPTION TO IMMEDIATELY REUSE THE PORT
	int opt = 1;
	if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		std::cout << "ERROR: setsockopt(SO_REUSEADDR)." << std::endl;
		return false;
	}
	// SETTING NON_BLOCKING SOCKET
	if (fcntl(_serverFd, F_SETFL, O_NONBLOCK) < 0) {
		std::cout << "ERROR: Unable to set non-blocking server socket" << std::endl;
		return false;
	}
	return true;
}

bool	Server::passBindListenErrors(sockaddr_in* serverAddress)
{
	if (bind(_serverFd, (struct sockaddr*)serverAddress, sizeof(*serverAddress)) < 0) {
		std::cout << "ERROR: Binding unsuccessfully." << std::endl;
		return false;
	}
	if (listen(_serverFd, SOMAXCONN) < 0) {
		std::cerr << "ERROR: Listening unable." << std::endl;
		return false;
	}
	return true;
}

void	Server::sendData(int targetFd)
{
	Client*		cl = getClientBySocket(targetFd);
	if (cl == NULL || !cl->hasOutput())
		return ;
	const std::string&	out = cl->getOutBuffer();
	int			bytes = send(targetFd, out.c_str(), out.length(), MSG_NOSIGNAL);
	if (bytes <= 0)
		disconnectClient(targetFd, "send failed");
	if (bytes > 0)
		cl->eraseOutBuffer(bytes);
	if (cl->getMarkForClose() && !cl->hasOutput())
		removeClient(targetFd);
}

void    Server::rebuildPollFds()
{
	_pollSockets.clear();

	struct pollfd	serverPoll;
	serverPoll.fd = _serverFd;
	serverPoll.events = POLLIN;
	serverPoll.revents = 0;
	_pollSockets.push_back(serverPoll);

	std::map<int, Client>::iterator it;
	for (it = _clients.begin(); it != _clients.end(); ++it) {
		struct pollfd clientPoll;
		clientPoll.fd = it->first;
		clientPoll.events = POLLIN;
		if (it->second.hasOutput())
			clientPoll.events |= POLLOUT;
		clientPoll.revents = 0;
		_pollSockets.push_back(clientPoll);
	}
	
}

void	Server::handlePollEvents()
{
	for (size_t i = 0; i < _pollSockets.size(); i++) {
		const int	currentFd = _pollSockets[i].fd;
		const short	revents = _pollSockets[i].revents;
		if (revents == 0)
			continue ;
		if (currentFd != _serverFd && getClientBySocket(currentFd) == NULL)
			continue ;
		if (revents & (POLLERR | POLLNVAL)) {
			if (currentFd != _serverFd)
				disconnectClient(currentFd, "socket error");
			continue ;
		}
		if (revents & POLLOUT)
			sendData(currentFd);
		if (revents & POLLIN) {
			if (currentFd == _serverFd)
				registerClient();
			else
				handleRawData(currentFd);
		}
		if ((revents & POLLHUP) && currentFd != _serverFd) {
			disconnectClient(currentFd, "connection closed by peer.");
		}
	}
}
