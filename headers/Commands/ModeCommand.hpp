#ifndef MODECOMMAND_HPP
#define MODECOMMAND_HPP

#include "ICommand.hpp"
#include <cstddef>
#include <string>
#include <vector>

class		Channel;
struct	Message;

enum	Action { ADD, REMOVE };

struct	Flag {
	char							flag;
	Action						act;
	bool							isParameterized;
	const std::string *parameterValue;
};

class	ModeCommand : public ICommand {

	private:
		static void	sendMessage(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	sendChannelDetails(Server &sv, Client& user, Channel& ch);
		static void	userVisibleOpt(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	userBanOpt(Server &sv, Client &user, Channel &ch);
		static void	banOpt(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	keyOpt(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	limitOpt(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	memberOpt(Server &sv, Client &user, Channel &ch, const Flag &f);
		static bool	takesParam(char flag, Action act);
		static void	getFlagArray(const Message &msg, std::vector<Flag>& flags);
		static void	applyFlag(Server &sv, Client &user, Channel &ch, const Flag &f);
		static void	parseChannelFlags(Server &sv, Client &user, Channel& ch, std::vector<Flag>& flags);
		static void	channelMode(Server &sv, Client &user, const Message &msg);
		static void	userMode(Server &sv, Client &user, const Message &msg);

	public:
		bool		needsRegistration() const;
		size_t	minParams() const;
		void		execute(Server &sv, Client &user, const Message &msg) const;
};

#endif
