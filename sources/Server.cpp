# include "Server.hpp"
# include <sys/socket.h>	// sockets
# include <arpa/inet.h>		// htons
# include <fcntl.h>			// fcntl
# include <iostream>		// containers
# include <cstring>			// memset
# include <unistd.h>		// close fds
# include <cerrno>			// EINTR, errno
# include <stdexcept>		// throw errors
# include <csignal>			// sig_atomic_t

extern volatile sig_atomic_t	g_serverRunning;

Server::Server(int port, const std::string& password)
	: _port(port),
	_password(password),
	_serverFd(-1) {}

Server::~Server()
{
	stop();
}

// PRIVATE COMMANDS

// 					SERVER UTILS

void    Server::rebuildPollFds()
{
	_pollSockets.clear();

	// INSERT THE LISTENER AS FIRST POSITION
	struct pollfd	serverPoll;
	serverPoll.fd = _serverFd;
	serverPoll.events = POLLIN;
	serverPoll.revents = 0;
	_pollSockets.push_back(serverPoll);

	// INSERT ALL THE ACTIVE CLIENTS
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

		// CHECKING RECEIVED EVENTS
		if (revents == 0)
			continue ;
		// CHECKING IF THE CLIENT IS STILL CONNECTED
		if (currentFd != _serverFd && getClientBySocket(currentFd) == NULL)
			continue ;
		// CHECKING FOR FATAL ERRORS
		if (revents & (POLLERR | POLLNVAL)) {
			if (currentFd != _serverFd)
				disconnectClient(currentFd, "socket error");
			continue ;
		}
		if (revents & POLLOUT)
			sendData(currentFd);
		// READING DATA FROM EVENT
		if (revents & POLLIN) {
			// IF SERVER SOCKET HAS POLLIN -> SOMEBODY IS TRYING TO CONNECT
			// ELSE SOMEBODY CONNECTED IS INTERACTING WITH THE SERVER
			if (currentFd == _serverFd)
				registerClient();
			else
				handleClientData(currentFd);
			continue ;
		}
		// PREMATURE ENDING CONNECTION
		if ((revents & POLLHUP) && currentFd != _serverFd) {
			disconnectClient(currentFd, "connection closed by peer.");
		}
	}
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

bool	Server::exceedsPendingLimit(size_t bytesRead, size_t maxBytes) {
	return bytesRead > maxBytes;
}

bool	Server::extractValidData(Client *target)
{
	std::string			rawLine;
	const int			fd = target->getSocket();
	size_t				pos;

	while ((pos = target->getBuffer().find('\n')) != std::string::npos)
	{
		rawLine = target->getBuffer().substr(0, pos);
		target->eraseBuffer(pos + 1);
		if (!rawLine.empty() && 
			rawLine[rawLine.size() - 1] == '\r'
		)
			rawLine.erase(rawLine.size() - 1);
		// PREVENT BUFFER OVERFLOW: 512 max octet per message permitted by IRC; 2 - '\r' + '\n'
		if (rawLine.size() + 2 > 512) {
			removeClient(fd);
			return false;
		} else if (rawLine.empty()) {
			// ITERATING AGAIN
			continue ;
		}
		// DISPATCH
		
		// CASE OF NOT PASS
		if (target->getMarkForClose())
		{
			if (!target->hasOutput())
				removeClient(fd);
			return false;
		}

		// SAFETY CASE OF COMMAND QUIT
		if (getClientBySocket(fd) == NULL)
			return false;
	}
	return true;
}

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

// 					SERVER INIT
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

//					SERVER INTERACT
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
		std::cout << "ERROR: Unable to set non-blocking client socket" << std::endl;
		close(clientFd);
		return ;
	}
	_clients.insert(std::make_pair(clientFd, Client(clientFd, host)));
	std::cout << "[SOCKET #" << clientFd << "] Successfully registered." << std::endl;
}

void	Server::removeClient(int clientFd)
{
	std::map<int, Client>::iterator	it = _clients.find(clientFd);
	
	// REMOVE USER FROM REGISTRY
	if (it == _clients.end()) 
		return ;
	_clients.erase(it);
	close(clientFd);
}

void	Server::handleClientData(int clientFd)
{
	static const size_t	RECV_SIZE = 1024;
	// MAX OCTETS TO RECEIVE
	static const size_t	MAX_PENDING = 512;
	Client*	target = getClientBySocket(clientFd);
	char	buffer[RECV_SIZE];
	ssize_t bytesRead;

	// SAFETY CONDITIONAL
	if (target == NULL)
		return ;
	bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
	if (bytesRead < 0) {
		disconnectClient(clientFd, "recv error");
		return ;
	}
	if (bytesRead == 0) {
		disconnectClient(clientFd, "peer closed connection");
		return ;
	}
	// COLLECT DATA
	target->appendBuffer(std::string(buffer, bytesRead));
	if (!extractValidData(target)) {
		return ;
	}
	// CHECK FOR FLOOD/SPAM MESSAGE
	if (exceedsPendingLimit(target->getBuffer().size(), MAX_PENDING)) {
		disconnectClient(clientFd, "spam");
		return ;
	}
}

Client*	Server::getClientBySocket(int fd)
{
	std::map<int, Client>::iterator it = _clients.find(fd);

	if (it == _clients.end())
		return NULL;
	return &it->second;
}


void	Server::sendData(int targetFd)
{
	Client*		cl = getClientBySocket(targetFd);
	if (cl == NULL || !cl->hasOutput())
		return ;
	const std::string&	out = cl->getOutBuffer();
	int			bytes = send(targetFd, out.c_str(), out.length(), 0);

	if (bytes > 0)
		cl->eraseOutBuffer(bytes);
	if (cl->getMarkForClose() && !cl->hasOutput())
		removeClient(targetFd);
}
// void	Server::sendMessageAll(int senderFd, const std::string& message);
// void	Server::sendMessagePrivate(int senderFd, int receiverFd, const std::string& message);
// void	Server::sendMessageChannel(int senderFd, const std::string& channelName, const std::string& message);

// PUBLIC COMMANDS

void	Server::run()
{
	if (!initServer())
		throw std::runtime_error("Initializing server failed.");

	while (g_serverRunning)
	{
		// RECONSTRUCT POLLFD
		rebuildPollFds();

		// CHECK EVENTS
		if (poll(&_pollSockets[0], _pollSockets.size(), -1) < 0)
		{
			// CATCHING THE SIGINT AND RELOAD LOOP
			if (errno == EINTR)
				continue ;
			throw std::runtime_error("poll failed.");
		}
		// PROCESSING EVENTS
		handlePollEvents();
	}
}

void	Server::stop()
{
	shutdownServer();
}

Channel*    Server::getChannel(const std::string& channelName)
{
	std::vector<Channel>::iterator  it;
	for (it = _channels.begin(); it != _channels.end(); it++)
	{
		if (it->getName() == channelName)
			return &(*it);
	}
	return NULL;
}
const std::string&	Server::getPassword() const { return _password; }

Client*	Server::getClientByNickname(const std::string& nickname)
{
	std::map<int, Client>::iterator	it;

	for (it = _clients.begin(); it != _clients.end(); it++) {
		if (it->second.getNickname() == nickname) {
			return &it->second;
		}
	}
	return NULL;
}

void	Server::setPassword(const std::string& password) { _password = password; }


bool	Server::isChannel(const std::string& name) const
{
	std::vector<Channel>::const_iterator  it;
	for (it = _channels.begin(); it != _channels.end(); it++)
	{
		if (it->getName() == name)
			return true;
	}
	return;
}

bool	Server::isClient(const std::string& name) const
{
	std::map<int, Client>::const_iterator	it;

	for (it = _clients.begin(); it != _clients.end(); it++) {
		if (it->second.getNickname() == name)
			return true;
	}
	return false;
}

void	Server::createChannel(Client* cl, const std::string& name)
{
	if (isChannel(name))
		return ;
	Channel ch(name);

	ch.addMember(cl, true);
	_channels.push_back(ch);
}


void	Server::sendTo(Client& cl, const std::string& text)
{
	cl.appendOutbuffer(text);
}

void	Server::broadcast(const Channel& ch, const std::string& msg, int exceptFd)
{
	if (!isChannel(ch.getName()))
		return ;
	
	std::map<int, Member>::const_iterator	it = ch.getAll().begin();
	while (it != ch.getAll().end())
	{
		if (exceptFd > 0 && it->first == exceptFd)
			continue ;
		sendTo(*it->second.cl, msg);
	}
}
