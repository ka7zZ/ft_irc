#ifndef INVITECOMMAND_HPP
# define INVITECOMMAND_HPP

# include "ICommand.hpp"
# include <vector>
# include <string>

class InviteCommand: public ICommand
{
	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

	private:
		void	splitParams(const std::string& list, std::vector<std::string>& out);
};

#endif