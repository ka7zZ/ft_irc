#pragma once

#include "irc.hpp"


typedef void (Server::*Handler)(Client&, std::vector<std::string>&);
typedef struct Record
{
	//void (Server::*handler)(Client&, std::vector<std::string>&);
	Handler handler;
	bool isAllowed;
} Record;



class Server
{
	private:
		// main member variable
		std::string	password;
		size_t		port;
		////////////////////////

		std::map<std::string, Record> table;
			
		std::vector<pollfd> fds;
		std::map<int , Client> clients;

		std::map<std::string , int> nicknames_to_fd;
		
		std::map<std::string, Channel> channels;	

		Server();
		Server(const Server&);
		Server&	operator=(const Server&);

	public:
		Server(size_t , std::string);

		~Server();
		void	generate_table();
	
		void	generic_handler(const generic_cmd&, Client&);
		
		//getters
		size_t get_port() const;	
		std::map<std::string, Record>& get_table();

		// setters
				std::map<std::string, Channel>& set_channel(std::string, Channel);	
		
		void	addClient(int);
		void	removeClient(int);
		
		// welcome notification
		void	welcome_msg(Client&);			
		// handlers function for commands
		void	handlePass(Client& ,std::vector<std::string>&);
		void	handleJoin(Client&, std::vector<std::string>&);
		void	handleNick(Client&, std::vector<std::string>&);
		void	handleUser(Client&, std::vector<std::string>&);
};

