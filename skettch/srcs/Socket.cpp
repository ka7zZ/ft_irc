#include "../headers/Socket.hpp"

Socket::Socket(int fd) : _fd(fd) {}

Socket::~Socket()
{
	if (_fd >= 0)
		close(_fd);
}

int	Socket::getFd() const
{
	return _fd;
}

ssize_t	Socket::send(const void* data, size_t size)
{
	return ::send(_fd, data, size, 0);
}

ssize_t	Socket::receive(void* buffer, size_t size)
{
	return ::recv(_fd, buffer, size, 0);
}
