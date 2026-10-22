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
	char				flag;
	Action				act;
	bool				isParameterized;
	const std::string*	parameterValue;
};

class ModeCommand: public ICommand
{
	private:
		static void	sendMessage(Server& sv, Client& user, Channel& ch, const Flag& f);
		static bool	isWithin(const std::string& flags, char	f);
		static void	getFlagArray(const Message& msg, std::vector<Flag> flags, const std::string& permittedFlags);
		static void	applyFlag(Server& sv, Client& user, Channel& ch, const Flag&f);
		static void	channelMode(Server& sv, Client& user, const Message& msg, std::vector<Flag> flags);
		static void	userMode(Server& sv, Client& user, const Message& msg);
		static void	memberStatus(Server& sv, Client& user, const Message& msg);

	public:
		bool   needsRegistration() const;
		size_t minParams() const;
		void   execute(Server& sv, Client& user, const Message& msg) const;

};

#endif
