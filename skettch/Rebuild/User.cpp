#include "User.hpp"

User::User() : _socketFd(-1), _isAuth(false), _passOk(false), _isNamed(false) {}

User::User(int socketFd) : _socketFd(socketFd), _isAuth(false), _passOk(false), _isNamed(false) {}

int	User::getSocket() const { return _socketFd; }

const std::string&	User::getNickname() const { return _nickname; }

const std::string&	User::getUsername() const { return _username; }

const std::string&	User::getPassword() const { return _password; }

const std::string&	User::getBuffer() const { return _inputBuffer; }

bool	User::isAuth() const { return _isAuth; }

bool	User::isNamed() const { return _isNamed; };

bool	User::passOk() const { return _passOk; }

void	User::setNickname(const std::string& nickname) { _nickname = nickname; }

void	User::setUsername(const std::string& username) { _username = username; }

void	User::setPassword(const std::string& password) { _password = password; }

void	User::setAuthenticated(bool auth) { _isAuth = auth; }

void	User::setAuth(bool isAuth) { _isAuth = isAuth; }

void	User::setNamed(bool isNamed) { _isNamed = isNamed; }

void	User::setPassOk(bool passOk) { _passOk = passOk; }

void	User::appendBuffer(const std::string& buf) { _inputBuffer += buf; }

void    User::eraseBuffer(size_t n) { _inputBuffer.erase(0, n); }

void	User::clearBuffer() { _inputBuffer.clear(); }
