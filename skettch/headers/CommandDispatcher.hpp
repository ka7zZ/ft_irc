#ifndef COMMANDDISPATCHER_HPP
# define COMMANDDISPATCHER_HPP

# include <string>
# include <map>
# include "ASTParser.hpp"

class Server;
class User;

class CommandDispatcher
{
	// DEFINE 
	public:
		typedef void (*CommandHandler)(Server&, User&, const MessageAST&);
	
	private:
		std::map<std::string, CommandHandler>   _handlers;

		static void handleNick(Server& server, User& user, const MessageAST& ast);
		static void handleUser(Server& server, User& user, const MessageAST& ast);
		static void handlePing(Server& server, User& user, const MessageAST& ast);
		static void handlePrivMsg(Server& server, User& user, const MessageAST& ast);

	public:
		CommandDispatcher();
		~CommandDispatcher();

		void	dispatch(Server& server, User& user, const MessageAST& ast);

};

#endif
