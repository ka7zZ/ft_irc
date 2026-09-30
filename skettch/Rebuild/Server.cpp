# include "Server.hpp"

extern volatile sig_atomic_t	g_serverRunning;

Server::Server(int port) : _port(port), _serverSocket(-1) {}

Server::~Server()
{
	stop();
}

// PRIVATE COMMANDS

// 					SERVER RUN/CLOSE UTILS
void    Server::reconstructPollFd()
{
	_pollSockets.clear();

	// INSERT THE LISTENER AS FIRST POSITION
	struct pollfd	serverPoll;
	serverPoll.fd = _serverSocket;
	serverPoll.events = POLLIN;
	serverPoll.revents = 0;
	_pollSockets.push_back(serverPoll);

	// INSERT ALL THE ACTIVE CLIENTS
	std::map<int, User>::iterator it;
	for (it = _users.begin(); it != _users.end(); ++it) {
		struct pollfd clientPoll;
		clientPoll.fd = it->first;
		clientPoll.events = POLLIN;
		clientPoll.revents = 0;
		_pollSockets.push_back(clientPoll);
	}
	
}

void	Server::processingEvents()
{
	for (size_t i = 0; i < _pollSockets.size(); i++) {
		int	currentSocket = _pollSockets[i].fd;
		short	revents = _pollSockets[i].revents;

		// CHECKING RECEIVED EVENTS
		if (revents == 0)
			continue;
		// CHECKING FOR FATAL ERRORS
		if (revents & (POLLERR | POLLNVAL)) {
			if (currentSocket != _serverSocket) {
				std::cout << "ERROR: Socket " << currentSocket << " will be deleted." << std::endl;
				removeClient(currentSocket);
			}
			continue ;
		}
		// READING DATA FROM EVENT
		if (revents & POLLIN) {
			// IF SERVER SOCKET HAS POLLIN -> SOMEBODY IS TRYING TO CONNECT
			// ELSE SOMEBODY CONNECTED IS INTERACTING WITH THE SERVER
			if (currentSocket == _serverSocket)
				registerClient();
			else
				handleClientData(currentSocket);
		}
		// PREMATURE ENDING CONNECTION
		if ((revents & POLLHUP) && !(revents & POLLIN)) {
			if (currentSocket != _serverSocket) {
				User* usr = getClientBySocket(currentSocket);
				if (usr)
					std::cout << "[" << usr->getNickname() << "]" << " Disconnected." << std::endl;
				else
					std::cout << "[UNKNOWN USER] Disconnected." << std::endl;
				removeClient(currentSocket);
			}
		}
	}
}

void	Server::closeServer()
{
	std::map<int, User>::iterator	it;
	int								clientSocket;

	for (it = _users.begin(); it != _users.end(); ++it) {
		clientSocket = it->first;
		close(it->first);
	}
	_users.clear();
	_pollSockets.clear();
	std::cout << "Successfully closed client's socket" << std::endl;
	if (_serverSocket != -1)
		close(_serverSocket);
	std::cout << "Server closed." << std::endl;
}

// 					SERVER INIT UTILS
bool	Server::passCreateErrors()
{
	if (_serverSocket < 0) {
		std::cerr << "ERROR: Server socket assigning failure." << std::endl;
		return false;
	}
	// SETTING SOCKET OPTION TO IMMEDIATELY REUSE THE PORT
	int opt = 1;
	if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		std::cerr << "ERROR: setsockopt(SO_REUSEADDR)." << std::endl;
		return false;
	}
	// SETTING NON_BLOCKING SOCKET
	if (fcntl(_serverSocket, F_SETFL, O_NONBLOCK) < 0) {
		std::cerr << "ERROR: Unable to set non-blocking socket" << std::endl;
		return false;
	}
	return true;
}

bool	Server::passBindListenErrors(sockaddr_in* serverAddress)
{
	if (bind(_serverSocket, (struct sockaddr*)serverAddress, sizeof(*serverAddress)) < 0) {
		std::cerr << "ERROR: Binding unsuccessfully." << std::endl;
		return false;
	}
	if (listen(_serverSocket, SOMAXCONN) < 0) {
		std::cerr << "ERROR: Listening unable." << std::endl;
		return false;
	}
	return true;
}

// 					SERVER INIT
bool	Server::initServer()
{
	// CREATE SERVER SOCKET
	_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
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
int		Server::handle_connections()
{
	struct sockaddr_in	clientAddress;
	socklen_t			clientLength = sizeof(clientAddress);
	int					clientSocket;
	unsigned long		clientIp;
	unsigned short		clientPort;

	// ACCEPTING THE CONNECT ATTEMPT
	clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddress, &clientLength);
	if (clientSocket < 0)
	{
		std::cout << "ERROR: Client socket." << std::endl;
		return -1;
	}

	// RETRIEVE CLIENT IP WITH IP DATATYPE
	clientIp = ntohl(clientAddress.sin_addr.s_addr);

	// NEXT WE'LL SET THE LIMIT OF ATTEMPTS TO 3
	// [] OPERATOR WILL AUTOMATICALLY INSERT THE KEY AND INITIALIZE ITS VALUE WITH 0 
	if (_failedAttempts[clientIp] >= 3)
	{
		std::cout << "ERROR: Connection denied: Too many attempts." << std::endl;
		close(clientSocket);
		return -1;
	}

	// WE ARE INCREMENTING THE ATTEMPT
	_failedAttempts[clientIp] += 1;

	// OBTAINING PORT & IP - LOCALLY WITH INET_NTOA
	clientPort = ntohs(clientAddress.sin_port);
	std::cout << "[Attempt to connect] " << inet_ntoa(clientAddress.sin_addr) << ":" << clientPort 
			<< "(" << _failedAttempts[clientIp] << ")." 
			<< std::endl;
	return clientSocket;
}

void	Server::registerClient()
{
	int	clientFd;
	
	clientFd = handle_connections();
	if (clientFd < 0)
		return ;
	_users.insert(std::make_pair(clientFd, User(clientFd)));

	std::cout << "[SOCKET " << clientFd << "] Successfully registered." << std::endl;
	
	struct pollfd	pfd;
	pfd.fd = clientFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollSockets.push_back(pfd);
}

void	Server::removeClient(int clientFd)
{
	std::map<int, User>::iterator	it = _users.find(clientFd);
	std::vector<struct pollfd>::iterator	psi;
	
	// REMOVE USER FROM REGISTRY
	if (it != _users.end()) {
		std::cout << "[SOCKET " << clientFd << "] Successfully removed." << std::endl
		_users.erase(it);
	}
	// REMOVE SOCKET FROM POLL
	for (psi = _pollSockets.begin(); psi != _pollSockets.end(); ++psi)
	{
		if (psi->fd = clientFd) {
			_pollSockets.erase(psi);
			break ;
		}
	}
}

void	Server::handleClientData(int clientFd)
{
	User*	target;
	int		maxBytes = 4096;
	ssize_t bytesRead;
	char	buffer[maxBytes];
	std::string	rawData;
	std::string	delimiter = "\r\n";
	size_t	delimiterPos;

	while (true)
	{
		bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);
		// CONDITIONAL CASE OF DISCONNECTED
		if (bytesRead == 0) {
			removeClient(clientFd);
			return ;
		}
		// CONDITIONAL CASE OF FRAGMENTED NETWORK PACKAGE
		if (bytesRead < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				break ;
			else {
				removeClient(clientFd);
				return ;
			}
		}
		// COLLECT DATA PHASE
		target = getClientBySocket(clientFd);
		target->appendBuffer(buffer);
		// ANTI FLOOD PROTECTION
		if (target->getBuffer().size() > maxBytes) {
			std::cout << "ERROR: Message too long for socket #" << clientFd
					<< ": Client disconnected."
			removeClient(clientFd);
		}
		// EXTRACT VALID DATA
		delimiterPos = target->getBuffer().find(delimiter);
		while (delimiterPos != std::string::npos)
		{
			rawData = target->getBuffer().substr(0, delimiterPos);
			// PREVENT BUFFER OVERFLOW/SPAM
			if (rawData.size() + 2 > 512) {
				std::cout << "ERROR: Attack prevented from socket #" << clientFd
						<< ". Client disconnected." << std::endl;
				removeClient(clientFd);
				return ;
			}
			// PARSING
			
			// DISPATCH

			// SAFETY CASE OF COMMAND QUIT
			if (getClientBySocket(clientFd) == NULL)
				return ;
		}
	}
}

User*	Server::getClientBySocket(int clientSocket);
User*	Server::getClientByNickname(const std::string& nickname);


void	Server::sendData(int targetFd, const std::string& message);
void	Server::sendMessageAll(int senderFd, const std::string& message);
void	Server::sendMessagePrivate(int senderFd, int receiverFd, const std::string& message);
void	Server::sendMessageChannel(int senderFd, const std::string& channelName, const std::string& message);

// PUBLIC COMMANDS

void	Server::run()
{
	if (!initServer())
	{
		std::cout << "ERROR: Initializing server failed." << std::endl;
		return ;
	}

	while (g_serverRunning)
	{
		// RECONSTRUCT POLLFD
		reconstructPollFd();

		// CHECK EVENTS
		int pollCount = poll(&_pollSockets[0], _pollSockets.size(), -1);
		if (pollCount < 0)
		{
			// CATCHING THE SIGINT AND RELOAD LOOP
			if (errno == EINTR)
				continue ;
			break ;
		}
		// PROCESSING EVENTS
		processingEvents();
	}
}

void	Server::stop()
{
	closeSockets();
}
