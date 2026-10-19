#ifndef MODECOMMAND_HPP
# define MODECOMMAND_HPP

# include "ICommand.hpp"
# include <string>
# include <vector>

class Channel;

class ModeCommand: public ICommand
{
	private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);

	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

};

#endif