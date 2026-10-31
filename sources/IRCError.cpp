# include "../headers/IRCError.hpp"

IRCError::IRCError(int code, const std::string& arg) : _code(code), _arg(arg) {}

IRCError::~IRCError() throw() {}

std::string IRCError::format(const std::string& arg) const
{
	std::ostringstream  oss;

	if (arg.empty())
		oss << ":ircserv " << _code << " * " << ": ";
	else
		oss << ":ircserv " << _code << " " << arg << ": ";

	switch (_code) {
		case 401:
			oss << "ERR_NOSUCHNICK: No such nick/channel";
			break;
		case 403:
			oss << "ERR_NOSUCHCHANNEL: No such channel";
			break ;
		case 404:
			oss << "ERR_CANNOTSENDTOCHAN: Cannot send to channel";
			break ;
		case 409:
			oss << "ERR_NOORIGIN: No origin specified";
			break ;
		case 411:
			oss << "ERR_NORECIPIENT: No origin specified";
			break ;
		case 412:
			oss << "ERR_NOTEXTTOSEND: No text to send";
			break ;
		case 421:
			oss << "ERR_UNKNOWNCOMMAND: Unknown command";
			break ;
		case 431:
			oss << "ERR_NONICKNAMEGIVEN: No nickname given";
			break ;
		case 432:
			oss << "ERR_ERRONEUSNICKNAME: Erroneous nickname";
			break ;
		case 433:
			oss << "ERR_NICKNAMEINUSE: Nickname is already in use";
			break ;
		case 441:
			oss << "ERR_USERNOTINCHANNEL: Nickname is already in use";
			break ;
		case 442:
			oss << "ERR_NOTONCHANNEL: You're not on that channel";
			break ;
		case 443:
			oss << "ERR_USERONCHANNEL: already on channel";
			break ;
		case 451:
			oss << "ERR_NOTREGISTERED: You have not registered";
			break ;
		case 461:
			oss << "ERR_NEEDMOREPARAMS: Not enough parameters";
			break ;
		case 462:
			oss << "ERR_ALREADYREGISTERED: You may not reregister";
			break ;
		case 464:
			oss << "ERR_PASSWDMISMATCH: Password incorrect";
			break ;
		case 467:
			oss << "ERR_KEYSET: Password incorrect";
			break ;
		case 471:
			oss << "ERR_CHANNELISFULL: Cannot join channel (+l)";
			break ;
		case 472:
			oss << "ERR_UNKNOWNMODE: Unknown mode char";
			break ;
		case 473:
			oss << "ERR_INVITEONLYCHAN: Cannot join channel (+i)";
			break ;
		case 474:
			oss << "ERR_BANNEDFROMCHAN: Cannot join channel (+b)";
			break ;
		case 475:
			oss << "ERR_BADCHANNELKEY: Cannot join channel (+k)";
			break ;
		case 476:
			oss << "ERR_BADCHANMASK: Bad Channel Mask";
			break ;
		case 482:
			oss << "ERR_CHANOPRIVSNEEDED: Bad Channel Mask";
			break ;
        case 501:
            oss << "ERR_UMODEUNKNOWNFLAG: Unknown MODE flag";
			break ;
        case 502:
            oss << "ERR_USERSDONTMATCH: Cannot change mode for other users";
			break ;
        case 525:
            oss << "ERR_INVALIDKEY: Invalid key";
			break ;
	}
	oss << "\r\n";
	return oss.str();
}
