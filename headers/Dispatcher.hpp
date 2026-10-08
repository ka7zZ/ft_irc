#ifndef DISPATCHER_HPP
# define DISPATCHER_HPP

# include "../headers/Commands/ICommand.hpp"
# include <map>
# include <string>
# include <iostream>

class Server;
class Client;
struct Message;

class Dispatcher
{
	private:
		std::map<std::string, ICommand*>	_commands;
		
		void	clear();

	public:
		Dispatcher();
		~Dispatcher();

		void    dispatch(Server& sv, Client& cl, const Message& msg);
};

#endif
