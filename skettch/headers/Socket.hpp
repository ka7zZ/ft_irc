#ifndef Socket_HPP
# define Socket_HPP

# include "ISocket.hpp"

class Socket : public ISocket
{
	private:
		int	_fd;

	public:
		explicit	Socket(int fd);
		virtual ~Socket();

		int	getFd() const;
		ssize_t	send(const void* data, size_t size);
		ssize_t	receive(void* buffer, size_t size);
};

#endif
