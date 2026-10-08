#ifndef ICOMMAND_HPP
# define ICOMMAND_HPP

# include <cstddef>

class Client;
class Server;
struct Message;

class ICommand
{
	public:
		virtual ~ICommand() {}

		virtual bool	needsRegistration() const = 0;
		virtual size_t	minParams() const = 0;
		virtual void	execute(Server&, Client&, const Message&) const = 0;
};

#endif