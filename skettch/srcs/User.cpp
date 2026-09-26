#include "../headers/User.hpp"

User::User(int id, ISocket* socket)
    : _id(id), _socket(socket), _isAuth(false) {}

User::~User() {}

// GETTERS
int User::getId() const
{
    return _id;
}

const std::string&	User::getNickname() const
{
    return _nickname;
}

const std::string&	User::getUsername() const
{
    return _username;
}

const std::string&	User::getRealname() const
{
    return _realname;
}

const std::string&	User::getHostname() const
{
    return _hostname;
}

bool	User::isAuth() const
{
    return _isAuth;
}

ISocket*	User::getSocket() const
{
    return _socket;
}

// SETTERS

void	User::setNickname(const std::string& nickname)
{
    _nickname = nickname;
}

void	User::setUsername(const std::string& username)
{
    _username = username;
}

void	User::setRealname(const std::string& realname)
{
    _realname = realname;
}

void	User::setHostname(const std::string& hostname)
{
    _hostname = hostname;
}

void	User::setAuthenticated(bool auth)
{
    _isAuth = auth;
}
