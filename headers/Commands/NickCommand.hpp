#ifndef NICKCOMMAND_HPP
# define NICKCOMMAND_HPP

# include "ICommand.hpp"
# include <cstddef>
# include <string>

class Server;
class Client;
struct Message;

class NickCommand: public ICommand
{
	private:
		static bool			isLegit(const std::string& nick);
	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;
};

#endif