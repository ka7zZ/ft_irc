#ifndef PONGCOMMAND_HPP
# define PONGCOMMAND_HPP

# include "ICommand.hpp"
# include <string>

class PongCommand: public ICommand
{
	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;
};

#endif