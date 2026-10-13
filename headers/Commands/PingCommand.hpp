#ifndef PINGCOMMAND_HPP
# define PINGCOMMAND_HPP

# include "ICommand.hpp"
# include <string>

class PingCommand: public ICommand
{
	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

};

#endif