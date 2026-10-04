#include "User.hpp"

User::User() :
    _socketFd(-1),
    _hasPass(false),
    _hasNick(false),
    _hasUser(false),
    _isAuth(false) {}

User::User(int socketFd) :
    _socketFd(socketFd),
    _hasPass(false),
    _hasNick(false),
    _hasUser(false),
    _isAuth(false) {}

int	User::getSocket() const { return _socketFd; }

const std::string&	User::getNickname() const { return _nickname; }

const std::string&	User::getUsername() const { return _username; }

const std::string&	User::getBuffer() const { return _inputBuffer; }

bool	User::getPassState() const { return _hasPass; }

bool	User::getNickState() const { return _hasNick; };

bool	User::getUserState() const { return _hasUser; };

bool	User::getAuthState() const { return _isAuth; }

void	User::setNickname(const std::string& nickname) { _nickname = nickname; }

void	User::setUsername(const std::string& username) { _username = username; }

void	User::setPassState(bool pass) { _hasPass = pass; }

void	User::setNickState(bool nick) { _hasNick; }

void	User::setUserState(bool user) { _hasUser; }

void	User::setAuthState(bool isAuth) { _isAuth = isAuth; }

void	User::appendBuffer(const std::string& buf) { _inputBuffer += buf; }

void    User::eraseBuffer(size_t n) { _inputBuffer.erase(0, n); }

void	User::clearBuffer() { _inputBuffer.clear(); }
