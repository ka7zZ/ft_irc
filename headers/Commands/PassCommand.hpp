#ifndef PASSCOMMAND_HPP
# define PASSCOMMAND_HPP

#include "ICommand.hpp"
#include <cstddef>

class Server;
class Client;
struct Message;

class PassCommand: public ICommand
{
	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;

};

#endif