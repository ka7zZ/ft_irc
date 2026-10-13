#ifndef PRIVMSGCOMMAND_HPP
# define PRIVMSGCOMMAND_HPP

# include "ICommand.hpp"
# include <cstddef>
# include <string>
# include <vector>

class Server;
class Client;
struct Message;

class PrivmsgCommand: public ICommand
{
	private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);
		static void	sendOne(Server& sv, Client& user, const std::string& recipient, const std::string& msg);

	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;
};


#endif