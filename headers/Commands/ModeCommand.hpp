#ifndef MODECOMMAND_HPP
# define MODECOMMAND_HPP

# include "ICommand.hpp"
# include <string>
# include <vector>
# include <cstddef>

class Channel;
struct Message;

enum Action {
	ADD,
	REMOVE
};

struct	Flag {
	char	flag;
	Action	act;
	bool	parameterized;
};

struct	ModeOption {
	Flag	f;
	size_t	paramIndex;
};

class ModeCommand: public ICommand
{
	private:
		static void		splitParams(const std::string& list, std::vector<std::string>& out);
		static void		throwErrors(Server& sv, Client& user, const Message& msg);
		static size_t	channelMode(Server& sv, Client& user, const Message& msg);
		static void		userMode(Server& sv, Client& user, const Message& msg);
		static void		memberStatus(Server& sv, Client& user, const Message& msg);

	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

};

#endif
