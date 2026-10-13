#ifndef KICKCOMMAND_HPP
# define KICKCOMMAND_HPP

# include "ICommand.hpp"
# include <string>
# include <vector>

class Channel;

class KickCommand: public ICommand
{
	private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);
		static void	kickOne(Server& sv, Client& op, const std::string& chanName, const std::string& nick, const Message& msg);

	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

};

#endif