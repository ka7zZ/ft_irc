#include "irc.hpp"


Client::Client(const Client& other)
{
	fd_socket = other.fd_socket;
	buffer = other.buffer;
	nickSet = other.nickSet;
	userSet = other.userSet;
	passOK = other.passOK;
	nickname = other.nickname;
	username = other.username;
}

Client& Client::operator=(const Client& other)
{
	if (this != &other)
	{
		fd_socket = other.fd_socket;
		buffer = other.buffer;
		nickSet = other.nickSet;
		userSet = other.userSet;
		passOK = other.passOK;
		nickname = other.nickname;
		username = other.username;
	}
	return (*this);
}


Client::Client()
{
}

Client::~Client()
{
}

Client::Client(int fd)
: fd_socket(fd)
{
	passOK = false;
	userSet = false;
	nickSet = false;
}


std::string& Client::get_outbuffer()
{
	return outbuffer;
}


std::string& Client::get_buffer()
{
	return buffer;
}


int Client::get_fd() const
{
	return (fd_socket);
}

bool Client::get_nickSet() const 
{
	return (nickSet);
}

bool Client::get_userSet() const 
{
	return (userSet);
}

bool Client::get_passOk() const 
{
	return (passOK);
}

std::string Client::get_username() 
{
	return (username);
}



std::string Client::get_nickname()
{
	return (nickname);
}


std::string Client::get_realname()
{
	return (realname);
}



void	Client::setPassOk(bool value)
{
		passOK = value;
}


void	Client::setUserSet(bool value)
{
	userSet = value;
}


void	Client::setNickSet(bool value)
{
	 nickSet = value;
}

void	Client::set_nickname(std::string& nick)
{
	nickname = nick;	
}

void	Client::set_username(std::string& user)
{
	username = user;
}


void	Client::set_realname(std::string& real)
{
	realname = real;
}






