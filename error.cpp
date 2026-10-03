#include "error.hpp"


std::map<std::string, Error> table_errors;


void generate_error_table()
{
	// Registration errors
	table_errors["ERR_NONICKNAMEGIVEN"]   = Error(431, "ERR_NONICKNAMEGIVEN",   "No nickname given");        // new
	table_errors["ERR_ERRONEUSNICKNAME"]  = Error(432, "ERR_ERRONEUSNICKNAME",  "Erroneous nickname");
	table_errors["ERR_NICKNAMEINUSE"]     = Error(433, "ERR_NICKNAMEINUSE",     "Nickname is already in use");
	table_errors["ERR_NOTREGISTERED"]     = Error(451, "ERR_NOTREGISTERED",     "You have not registered");
	table_errors["ERR_NEEDMOREPARAMS"]    = Error(461, "ERR_NEEDMOREPARAMS",    "Not enough parameters");
	table_errors["ERR_ALREADYREGISTERED"] = Error(462, "ERR_ALREADYREGISTERED", "You may not reregister");
	table_errors["ERR_PASSWDMISMATCH"]    = Error(464, "ERR_PASSWDMISMATCH",    "Password incorrect");

	// JOIN errors
	table_errors["ERR_NOSUCHCHANNEL"]     = Error(403, "ERR_NOSUCHCHANNEL",     "No such channel");
	table_errors["ERR_CHANNELISFULL"]     = Error(471, "ERR_CHANNELISFULL",     "Cannot join channel (+l)");
	table_errors["ERR_INVITEONLYCHAN"]    = Error(473, "ERR_INVITEONLYCHAN",    "Cannot join channel (+i)");
	table_errors["ERR_BADCHANNELKEY"]     = Error(475, "ERR_BADCHANNELKEY",     "Cannot join channel (+k)");
	table_errors["ERR_BADCHANMASK"]       = Error(476, "ERR_BADCHANMASK",       "Bad Channel Mask");         // new

	// PRIVMSG errors
	table_errors["ERR_NOSUCHNICK"]        = Error(401, "ERR_NOSUCHNICK",        "No such nick/channel");
	table_errors["ERR_CANNOTSENDTOCHAN"]  = Error(404, "ERR_CANNOTSENDTOCHAN",  "Cannot send to channel");   // new
	table_errors["ERR_NORECIPIENT"]       = Error(411, "ERR_NORECIPIENT",       "No recipient given");
	table_errors["ERR_NOTEXTTOSEND"]      = Error(412, "ERR_NOTEXTTOSEND",      "No text to send");

	// KICK / INVITE / TOPIC / MODE errors
	table_errors["ERR_USERNOTINCHANNEL"]  = Error(441, "ERR_USERNOTINCHANNEL",  "They aren't on that channel"); // new
	table_errors["ERR_NOTONCHANNEL"]      = Error(442, "ERR_NOTONCHANNEL",      "You're not on that channel");
	table_errors["ERR_USERONCHANNEL"]     = Error(443, "ERR_USERONCHANNEL",     "is already on channel");    // new
	table_errors["ERR_KEYSET"]            = Error(467, "ERR_KEYSET",            "Channel key already set");  // new
	table_errors["ERR_UNKNOWNMODE"]       = Error(472, "ERR_UNKNOWNMODE",       "is unknown mode char to me"); // new
	table_errors["ERR_CHANOPRIVSNEEDED"]  = Error(482, "ERR_CHANOPRIVSNEEDED",  "You're not channel operator");

	// Generic
	table_errors["ERR_NOORIGIN"]          = Error(409, "ERR_NOORIGIN",          "No origin specified");      // new
	table_errors["ERR_UNKNOWNCOMMAND"]    = Error(421, "ERR_UNKNOWNCOMMAND",    "Unknown command");
}
