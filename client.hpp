#pragma once

#include "irc.hpp"

class Client {
	private:
		int	fd_socket;
		std::string buffer;
		std::string outbuffer;
		bool nickSet;
		bool userSet;
		bool passOK;
		std::string nickname;
		std::string username;
		std::string realname;
	public:

		Client();
		Client(int fd);
		Client(const Client&);
		Client& operator=(const Client&);
		~Client();
		// setters
		std::string& get_buffer();
		std::string& get_outbuffer();
		void	setPassOk(bool);
		void	setUserSet(bool);
		void	setNickSet(bool);

		void	set_nickname(std::string&);
		void	set_username(std::string&);
		void	set_realname(std::string&);


		// getters

		int get_fd() const;
		bool get_nickSet() const ;
		bool get_userSet() const ;
		bool get_passOk() const ;
		std::string get_username()  ;
		std::string get_nickname()  ;
		std::string get_realname()  ;

		

};


