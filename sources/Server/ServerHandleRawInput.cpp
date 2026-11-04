# include "../../headers/Server.hpp"
# include "../../headers/IRCParser.hpp"
# include "../../headers/Dispatcher.hpp"

bool	Server::exceedsPendingLimit(size_t bytesRead, size_t maxBytes) {
	return bytesRead > maxBytes;
}

bool	Server::parseRawData(Client *target)
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
		if (rawLine.size() + 2 > 512) {
			removeClient(fd);
			return false;
		} else if (rawLine.empty()) {
			continue ;
		}
		try {
			IRCParser	parser(rawLine);
			Message		msg = parser.parseTokens();
			if (!msg.command.empty())
				_dispatcher.dispatch(*this, *target, msg);
		} catch (std::exception& e) {
			return false;
		}
		if (target->getMarkForClose())
		{
			if (!target->hasOutput())
				removeClient(fd);
			return false;
		}
		if (getClientBySocket(fd) == NULL)
			return false;
	}
	return true;
}

void	Server::handleRawData(int clientFd)
{
	static const size_t	RECV_SIZE = 1024;
	static const size_t	MAX_PENDING = 512;
	Client*	target = getClientBySocket(clientFd);
	char	buffer[RECV_SIZE];
	ssize_t bytesRead;

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
	target->appendBuffer(std::string(buffer, bytesRead));
	if (!parseRawData(target)) {
		return ;
	}
	if (exceedsPendingLimit(target->getBuffer().size(), MAX_PENDING)) {
		disconnectClient(clientFd, "spam");
		return ;
	}
}

void	Server::sendTo(Client& cl, const std::string& text)
{
	cl.appendOutbuffer(text);
}