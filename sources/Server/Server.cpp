# include "../../headers/Server.hpp"
# include <cerrno>			// EINTR, errno
# include <stdexcept>		// throw errors
# include <csignal>			// sig_atomic_t

extern volatile sig_atomic_t	g_serverRunning;

Server::Server(int port, const std::string& password)
	: _port(port),
	_password(password),
	_serverFd(-1),
	_counter(0) {}

Server::~Server()
{
	stop();
}

const std::string&	Server::getPassword() const { return _password; }

void	Server::setPassword(const std::string& password) { _password = password; }

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
			if (errno == EINTR) {
				if (!g_serverRunning)
					break;
				continue ;
			}	
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
