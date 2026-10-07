#ifndef USERCOMMAND_HPP
# define USERCOMMAND_HPP

# include "ICommand.hpp"
# include <cstddef>
# include <string>

class Server;
class Client;
struct Message;

class UserCommand: public ICommand
{

	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;
};

#endif