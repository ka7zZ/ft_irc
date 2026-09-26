#ifndef TCPSOCKET_HPP
# define TCPSOCKET_HPP

# include "ISocket.hpp"

class TCPSocket : public ISocket
{
	private:
		int	_fd;

	public:
		explicit	TCPSocket(int fd);
		virtual ~TCPSocket();

		int	getFd() const;
		ssize_t	send(const void* data, size_t size);
		ssize_t	receive(void* buffer, size_t size);
};

#endif
