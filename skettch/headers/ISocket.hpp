#ifndef ISOCKET_HPP
# define ISOCKET_HPP

# include <string>
# include <sys/socket.h>
# include <unistd.h>

class ISocket {
	public:
		virtual ~ISocket() {};

		virtual int getFd() const = 0;
		virtual ssize_t	send(const void* data, size_t size) = 0;
		virtual ssize_t	receive(void *buffer, size_t size) = 0;
};

#endif