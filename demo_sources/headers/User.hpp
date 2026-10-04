#ifndef USER_HPP
# define USER_HPP

# include <set>
# include <string>

class Channel;
class Socket;

class User
{
	public:
		User(const std::string &nick, const std::string &username,
			 const std::string &realname, Socket *socket);

		std::string nick;
		std::string username;
		std::string realname;
		Socket *socket;
		std::set<Channel *> channels;
};

#endif