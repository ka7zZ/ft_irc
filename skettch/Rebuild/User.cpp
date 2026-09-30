#include "User.hpp"

User::User() {}

User::User(int socketFd) : _socketFd(socketFd) {}

int	User::getSocketFd() const { return _socketFd; }

const std::string&	User::getNickname() const { return _nickname; }

const std::string&	User::getUsername() const { return _username; }

const std::string&	User::getRealname() const { return _realname; }

const std::string&	User::getBuffer() const { return _inputBuffer; }

bool	User::isAuth() const { return _isAuth; }

void	User::setNickname(const std::string& nickname) { _nickname = nickname; }

void	User::setUsername(const std::string& username) { _username = username; }

void	User::setRealname(const std::string& realname) { _realname = realname; }

void	User::setAuthenticated(bool auth) { _isAuth = auth; }

void	User::appendBuffer(const std::string& buf) { _inputBuffer += buf; }

void    User::eraseBuffer(size_t start, size_t length) { _inputBuffer.erase(start, length); }

void	User::clearBuffer() { _inputBuffer.clear(); }
