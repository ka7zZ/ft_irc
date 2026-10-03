#include "irc.hpp"

void	Server::welcome_msg(Client& client)
{
	client.get_outbuffer() += ":server 001 " + client.get_nickname() + " :Welcome to the Internet Relay Network " +  client.get_nickname() + "!" + client.get_username() + "@host" +"\r\n";	
	client.get_outbuffer() += ":server 002 " + client.get_nickname() + " :Your host is server, running version 1.0" + "\r\n";
	client.get_outbuffer() += ":server 003 " + client.get_nickname() + " :This server was created ..." + "\r\n";
	client.get_outbuffer() += ":server 004 " + client.get_nickname() +  " server 1.0 +iw +itkol" + "\r\n";

}

Server::~Server()
{}
// getters
size_t Server::get_port() const
{
	return (port);
}

void	Server::addClient(int fd)
{
    clients.insert(std::make_pair(fd, Client(fd)));
    pollfd newEntry;
    newEntry.fd = fd;
    newEntry.events = POLLIN;
    newEntry.revents = 0;
    fds.push_back(newEntry);
}

void	Server::removeClient(int fd)
{
	// REMOVE THE CLIENT WHO HAS THE INDEX FROM map<INT , CLIENT>
	clients.erase(fd);
	
	// remove the client 
	for (size_t i = 0; i < fds.size(); i++)
	{
		if (fds[i].fd == fd)
		{
			fds.erase(fds.begin() + i);
			break;
		}
	}
	// close teh connectiom with that specific client
	close (fd);
}


std::map<std::string, Record>& Server::get_table() 

{
	return (table);
}

void Server::generate_table()
{
	table["PASS"] = (Record){ &Server::handlePass, true };

	
	table["NICK"] = (Record){ &Server::handleNick, true };
	
	table["USER"] = (Record){ &Server::handleUser, true };
	table["JOIN"] = (Record){ &Server::handleJoin, false };
	
	/*
	table["PRIVMSG"] = (Record){ &Server::handlePrivMsg, false };
	table["KICK"] = (Record){ &Server::handleKick, false };
	table["INVITE"] = (Record){ &Server::handleInvite, false };
	table["TOPIC"] = (Record){ &Server::handleTopic, false };
	table["MODE"] = (Record){ &Server::handleMode, false };
	table["PART"] = (Record){ &Server::handlePart, false };
	table["QUIT"] = (Record){ &Server::handleQuit, false };
	table["PING"] = (Record){ &Server::handlePing, false };
	table["PONG"] = (Record){ &Server::handlePong, false };
	*/
}




void	Server::generic_handler(const generic_cmd& g_cmd , Client& client)
{	
	std::string cmd_name;
	std::string msg;
	std::vector<std::string> args;

	cmd_name =  g_cmd.cmd_name;
	args = g_cmd.cmd_args;

	std::cout << cmd_name << std::endl;
	// search for the name command in the table JOIN
	std::map<std::string, Record>::iterator it = table.find(cmd_name); // cmd_name = JOIN
 
	
	// if the command not in the table
	if (it == table.end())
	{

		//msg  = table_errors[421].msg;
		// send the  to the client like  error 421 ERR_UNKNOWNCOMMAND
		//send(client.get_fd(), msg.c_str(), msg.length(), 0);
		return ;

	}


	// when the command is not allowed that means he needs registration first 
	if ( !it->second.isAllowed)
	{
		bool isRegistred =  client.get_passOk() & client.get_userSet() & client.get_nickSet(); 
		if (!isRegistred)
		{
			//msg  = table_errors[451].msg;
			// send the  to the client like  error  451  ERR_NOTREGISTERED 
			//send(fds[i].fd, msg, msg.length(), 0);
			//  close the server
			return ;
		}
		(this->*(it->second.handler))(client, args);

	}
	else // for PASS, USER , NICK
	{
		(this->*(it->second.handler))(client, args);
	}
	
}



Server::Server(size_t _port, std::string _password)
{
	port = _port;
	password = _password;
	generic_cmd g_cmd;

	// Create socket
	int	server_fd = socket(AF_INET, SOCK_STREAM, 0);

	// Give it an address
	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(get_port()); // 6667

	
	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)  // ??????????????
    		perror("setsockopt");

	bind(server_fd, (sockaddr*)&address, sizeof(address));


	// Start listening
	//
	listen(server_fd, 5);

	std::cout  << "Waiting for a client...\n";


	pollfd server_poll;

	server_poll.fd = server_fd;
	server_poll.events = POLLIN;
	server_poll.revents = 0;

	
	fds.push_back(server_poll);
	
	unsigned int number_fds = 1;
	generate_error_table();
	// table of command	
	generate_table();
	
	while (true)
	{

		//int number_fds_poll = fds.size();
		
		// POLL FUNCTION
		poll(fds.data(), number_fds, -1);

		for (unsigned int i = 0; i < number_fds; i++)
		{
			
			int actual_fd  = fds[i].fd;
			if (fds[i].revents & POLLOUT)
			{
				std::string& outbuffer = clients[actual_fd].get_outbuffer();
				
				int bytes = send(actual_fd, outbuffer.c_str(), outbuffer.length(), 0);
				if (bytes > 0)
					outbuffer.erase(0, bytes);
				if (outbuffer.empty())
           			 	fds[i].events &= ~POLLOUT;				
			}

			
			if (fds[i].revents & POLLIN)
			{
				if (actual_fd == server_fd)
				{
							
					int	client_fd = accept(server_fd, NULL, NULL);			
					addClient(client_fd);
					number_fds++;
				}
				
				else
				{
					char	tmp_buffer[1024];

					memset(tmp_buffer, 0, sizeof(tmp_buffer));
					int bytes = recv(actual_fd, tmp_buffer, sizeof(tmp_buffer), 0);
					if (bytes > 0) // tha means any client has send something like hello
					{
	
						std::cout << "I receive something from client ID " <<  actual_fd << " " << tmp_buffer;
						clients[actual_fd].get_buffer() += std::string(tmp_buffer, bytes);
						size_t pos ;
						while ( (pos = clients[actual_fd].get_buffer().find("\r\n")) != std::string::npos) // i found the first command line
						{
							std::string cmd_line = clients[actual_fd].get_buffer().substr(0, pos);
							
						
							clients[actual_fd].get_buffer().erase(0 , pos + 2); // 2 id for "\r\n"
							if (cmd_line.empty())
								continue;

							
							// here i need to call the parsing
							g_cmd =  Parser::parser(cmd_line);

							//fds[i].events = POLLIN | POLLOUT;
							generic_handler(g_cmd, clients[actual_fd]);
							if (!clients[actual_fd].get_outbuffer().empty())
    								fds[i].events |= POLLOUT;
								
							
						}
						
					}
					if (bytes == 0) // or the client just press enter without send anything meaninh he want to close the connection
					{	
						removeClient(fds[i].fd);
						number_fds--;
						i--;
						continue;
					}
				}

			}
				
		}

		
	}
	
}

