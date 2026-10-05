#ifndef JOINCOMMAND_HPP
# define JOINCOMMAND_HPP

# include "ICommand.hpp"
# include <vector>
# include <string>

class JoinCommand: public ICommand
{
	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

	private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);
		static bool	validName(const std::string& channelName);
		static bool	freeSpace(size_t limit, size_t membes);
		static void	sendReplies(Server& sv, Client& cl, const Channel& ch);
		static void	joinChannel(Server& sv, Client& cl, const std::string& name, const std::string& key);
		
};

#endif