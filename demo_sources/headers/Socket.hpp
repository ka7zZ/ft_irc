#ifndef SOCKET_HPP
# define SOCKET_HPP

# include <string>

class User;

class Socket
{
	public:
		enum RegistrationState
		{
			UNREGISTERED,
			NICK_SEEN,
			REGISTERED
		};

		explicit Socket(int fd);
		~Socket();

		int getFd() const;

		int fd;
		std::string inputBuffer;
		std::string outputBuffer;
		RegistrationState registrationState;
		std::string nick;
		std::string username;
		std::string realname;
		User *user;
};

#endif
