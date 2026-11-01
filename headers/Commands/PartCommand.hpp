#ifndef PARTCOMMAND_HPP
# define PARTCOMMAND_HPP

# include "ICommand.hpp"
# include <vector>
# include <string>

class Channel;

class PartCommand: public ICommand
{
    private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);
		
	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;
};

#endif