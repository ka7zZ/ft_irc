#include "../headers/Client.hpp"

Client::Client() :
    _socketFd(-1),
    _hasPass(false),
    _hasNick(false),
    _hasUser(false),
    _isAuth(false),
    _closeAfterFlush(false) {}

Client::Client(int socketFd, const std::string& hostname)
    :
    _socketFd(socketFd),
    _hostname(hostname),
    _hasPass(false),
    _hasNick(false),
    _hasUser(false),
    _isAuth(false),
    _closeAfterFlush(false) {}

int	Client::getSocket() const { return _socketFd; }

const std::string&	Client::getNickname() const { return _nickname; }

const std::string&	Client::getUsername() const { return _username; }

const std::string&	Client::getBuffer() const { return _inputBuffer; }

const std::string&	Client::getOutBuffer() const { return _outBuffer; }

bool	Client::getPassState() const { return _hasPass; }

bool	Client::getNickState() const { return _hasNick; };

bool	Client::getUserState() const { return _hasUser; };

bool	Client::getAuthState() const { return _isAuth; }

void	Client::setNickname(const std::string& nickname) { _nickname = nickname; }

void	Client::setUsername(const std::string& username) { _username = username; }

void	Client::setPassState(bool pass) { _hasPass = pass; }

void	Client::setNickState(bool nick) { _hasNick; }

void	Client::setUserState(bool user) { _hasUser; }

void	Client::setAuthState(bool isAuth) { _isAuth = isAuth; }

std::string Client::prefix() const { return _nickname + "!" + _username + "@" + _hostname; }

void	Client::appendBuffer(const std::string& buf) { _inputBuffer += buf; }

void    Client::eraseBuffer(size_t n) { _inputBuffer.erase(0, n); }

void	Client::clearBuffer() { _inputBuffer.clear(); }

void	Client::appendOutbuffer(const std::string& add) { _outBuffer += add; }

void	Client::eraseOutBuffer(size_t n) { _outBuffer.erase(0, n); }

bool	Client::hasOutput() const { return !_outBuffer.empty(); }
