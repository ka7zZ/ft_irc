#include "../headers/Server.hpp"
#include "../headers/TCPSocket.hpp"
#include "../headers/Lexer.hpp"
#include "../headers/ASTParser.hpp"

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

Server::Server(int port)
	: _port(port), _isRunning(false), _serverFd(-1) {}

Server::~Server()
{
	stop();
}

bool    Server::initServer()
{
	// CREATE SERVER SOCKET
	_serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (_serverFd < 0)
	{
		std::cerr << "ERROR: Server socket failure" << std::endl;
		return false;
	}

	// SET SOCKET OPTIONS
	int	opt = 1;
	setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// SET ADDRESS
	struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(_port);

	// BIND SERVER SOCKET TO ADDRESS
	if (bind(_serverFd, (struct sockaddr*)&address, sizeof(address)) < 0)
	{
		std::cerr << "ERROR: Binding." << std::endl;
		return false;
	}
	if (listen(_serverFd, SOMAXCONN) < 0)
	{
		std::cerr << "ERROR: Listening." << std::endl;
		return false;

	}
	// NON-BLOCKING
	fcntl(_serverFd, F_SETFL, O_NONBLOCK);

	// ADD TO POLL
	struct pollfd	pfd;
	pfd.fd = _serverFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);

	return true;
}

void	Server::acceptClient()
{
	struct sockaddr_in	clientAddr;
	socklen_t			clientLen = sizeof(clientAddr);
	int					clientFd = accept(_serverFd, (struct sockaddr*)&clientAddr, &clientLen);

	if (clientFd < 0)
		return;
	fcntl(clientFd, F_SETFL, O_NONBLOCK);

	TCPSocket* socket = new TCPSocket(clientFd);
	User* user = new User(clientFd, socket);

	_users[clientFd] = user;

	struct pollfd	pfd;
	pfd.fd = clientFd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	_pollFds.push_back(pfd);

	std::cout << "[NEW CLIENT] Connected on: " << clientFd << " fd" << std::endl;
}

void	Server::handleClientData(int clientFd)
{
	char	buffer[1024];
	ssize_t	bytesRead = _users[clientFd]->getSocket()->receive(buffer, sizeof(buffer) - 1);

	if (bytesRead <= 0)
	{
		removeClient(clientFd);
		return ;
	}
	buffer[bytesRead] = '\0';
	std::string	rawData(buffer);

	Lexer lxr(rawData);
	std::vector<Token>	tks = lxr.tokenize();

	ASTParser	parser(tks);
	MessageAST*	ast = parser.parse();

	if (ast && !ast->command.empty()) {
		std::cout << "[fd " << clientFd << "] Command :" << ast->command << "approved!" << std::endl;
		_dispatcher.dispatch(*this, *_users[clientFd], *ast);
	}
	delete ast;
}

void	Server::removeClient(int clientFd)
{
	std::cout << "[fd " << clientFd << "] Disconnected." << std::endl;
	std::map<int, User*>::iterator it = _users.find(clientFd);
	if (it != _users.end())
	{
		delete it->second;
		_users.erase(it);
	}

	for (std::vector<struct pollfd>::iterator pit = _pollFds.begin(); pit != _pollFds.end(); pit++)
	{
		if (pit->fd == clientFd)
		{
			_pollFds.erase(pit);
			break ;
		}
	}
}

void	Server::start()
{
	if (!initServer())
		return ;
	
	_isRunning = true;
	std::cout << "Server opened successfully at port: " << _port << std::endl;

	while (_isRunning)
	{
		// WAITING FOR EVENTS - -1 -> BLOCKED UNTIL A NEW EVENT
		int pollCount = poll(&_pollFds[0], _pollFds.size(), -1);
		if (pollCount < 0)
		{
			std::cerr << "ERROR: poll()" << std::endl;
			break ;
		}

		// GOING THRU THE FDS TO VERIFY INPUT
		for (size_t i = 0; i < _pollFds.size(); i++) {
			if (_pollFds[i].revents & POLLIN)
			{
				if (_pollFds[i].fd == _serverFd)
				{
					// A CLIENT ARE TRYING TO CONNECT WITHIN OUR IRC
					acceptClient();
				}
				else
				{
					// A CLIENT IS COMMUNICATING ON OUR IRC
					handleClientData(_pollFds[i].fd);
				}
			}
		}
	}
}

void	Server::stop()
{
	_isRunning = false;
	for (std::map<int, User*>::iterator it = _users.begin(); it != _users.end(); ++it)
	{
		delete it->second;
	}
	_users.clear();
	_pollFds.clear();

	if (_serverFd >= 0) {
		close(_serverFd);
		_serverFd = -1;
	}
}

void	Server::addUser(User* user)
{
	if (user != NULL)
		_users[user->getId()] = user;
}

void	Server::removeUser(int id)
{
	_users.erase(id);
}

User*	Server::getUserByNick(const std::string& nickname)
{
	for (std::map<int, User*>::iterator it = _users.begin(); it != _users.end(); ++it)
	{
		if (it->second->getNickname() == nickname)
			return it->second;
	}
	return NULL;
}

bool	Server::isRunning() const
{
	return _isRunning;
}
