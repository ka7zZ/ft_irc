#ifndef TOPICCOMMAND_HPP
# define TOPICCOMMAND_HPP

# include "ICommand.hpp"

class TopicCommand : public ICommand
{
	public:
		bool	needsRegistration() const;
		size_t	minParams() const;
		void	execute(Server& sv, Client& user, const Message& msg) const;
};

#endif
