#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <vector>
#include "parser.hpp"
#include "server.hpp"


int main()
{
	// Create socket
	int	server_fd = socket(AF_INET, SOCK_STREAM, 0);

	// Give it an address
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(6667);


	bind(server_fd, (sockaddr*)&address, sizeof(address));


	// Start listening
	//
	listen(server_fd, 5);

	std::cout  << "Waiting for a client...\n";


	pollfd server_poll;

	server_poll.fd = server_fd;
	server_poll.events = POLLIN;
	server_poll.revents = 0;

	std::vector<pollfd> fds;
	
	fds.push_back(server_poll);
	
	unsigned int number_fds = 1;
	
	std::map<int , client> cls;

	std::vector<pollfd> fds;
	
	while (true)
	{
		//int number_fds_poll = fds.size();
		
		// POLL FUNCTION
		poll(fds.data(), number_fds, -1);


		for (unsigned int i = 0; i < number_fds; i++)
		{
			
			if (fds[i].revents & POLLIN)
			{
				if (fds[i].fd == server_fd)
				{
							
					int	client_fd = accept(server_fd, NULL, NULL);			
					addClient(client_fd);
					number_fds++;
				}
				
				else
				{
					//std::string	client_buffer;
					char tmp_buffer[1024];
					memset(tmp_buffer, 0, sizeof(tmp_buffer));
					int bytes = recv(fds[i].fd, tmp_buffer, 1024, 0);
				
					if (bytes > 0) // tha means any client has send something like hello
					{
						int actual_fd = fds[i].fd;
	
						std::cout << "I receive something from client ID " <<  actual_fd << " " << buffer;
						//send(fds[i].fd, "Welcome\n", 8, 0);
						cls[actual_fd].get_buffer() += std::string(tmp_buffer, bytes);
						size_t pos ;
						while ( (pos = cls[actual_fd].get_buffer().find("\r\n")) != std::string::npos) // i found the first command line
						{
							std::string cmd_line = cls[actual_fd].get_buffer().substr(0, pos);
							cls[actual_fd].get_buffer().erase(0 , pos + 2); // 2 id for "\r\n"
							
							
							// here i need to call the parsing
							generic_cmd g_cmd =  Parser::parser(cls[actual_fd].get_buffer());
							generic_handler(g_cmd);
							
						}
						
					}
					if (bytes == 0) // or the client just press enter without send anything meaninh he want to close the connection
					{
						/*
						close(fds[i].fd);
						fds.erase(fds.begin() + i);
						*/
						
						removeClient(fds[i].fd);
						number_fds--;
					}
			}

			}
				
	
		}

		
	}
	// Accept a client






	close(server_fd);
}
