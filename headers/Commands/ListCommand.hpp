#ifndef LISTCOMMAND_HPP
# define LISTCOMMAND_HPP

# include "ICommand.hpp"
# include <string>
# include <vector>

class ListCommand : public ICommand
{
	private:
		static void	splitParams(const std::string& list, std::vector<std::string>& out);
		static bool	isRequested(const std::vector<std::string>& requested, const std::string& channelName);

	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;
};

#endif
