#include "../headers/TCPSocket.hpp"

TCPSocket::TCPSocket(int fd) : _fd(fd) {}

TCPSocket::~TCPSocket()
{
	if (_fd >= 0)
		close(_fd);
}

int	TCPSocket::getFd() const
{
	return _fd;
}

ssize_t	TCPSocket::send(const void* data, size_t size)
{
	return ::send(_fd, data, size, 0);
}

ssize_t	TCPSocket::receive(void* buffer, size_t size)
{
	return ::recv(_fd, buffer, size, 0);
}
