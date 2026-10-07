#include "../headers/Client.hpp"

Client::Client() :
    _socketFd(-1),
    _hasPass(false),
    _hasNick(false),
    _isAuth(false),
    _closeAfterFlush(false) {}

Client::Client(int socketFd, const std::string& hostname)
    :
    _socketFd(socketFd),
    _hostname(hostname),
    _hasPass(false),
    _hasNick(false),
    _isAuth(false),
    _closeAfterFlush(false),
    _markForClose(false) {}

int	Client::getSocket() const { return _socketFd; }

const std::string&	Client::getNickname() const { return _nickname; }

const std::string&	Client::getUsername() const { return _username; }

const std::string&	Client::getRealname() const { return _realname; }

const std::string&	Client::getBuffer() const { return _inputBuffer; }

const std::string&	Client::getOutBuffer() const { return _outBuffer; }

bool	Client::getPassState() const { return _hasPass; }

bool	Client::getNickState() const { return _hasNick; };

bool	Client::getAuthState() const { return _isAuth; }

bool	Client::getMarkForClose() const { return _markForClose; }

void	Client::setNickname(const std::string& nickname) { _nickname = nickname; }

void	Client::setUsername(const std::string& username) { _username = username; }

void	Client::setRealname(const std::string& realname) { _realname = realname; }

void	Client::setPassState(bool pass) { _hasPass = pass; }

void	Client::setNickState(bool nick) { _hasNick; }

void	Client::setAuthState(bool isAuth) { _isAuth = isAuth; }

void	Client::setMarkForClose(bool mark) { _markForClose = mark; }

std::string Client::prefix() const { return _nickname + "!" + _username + "@" + _hostname; }

void	Client::appendBuffer(const std::string& buf) { _inputBuffer += buf; }

void    Client::eraseBuffer(size_t n) { _inputBuffer.erase(0, n); }

void	Client::clearBuffer() { _inputBuffer.clear(); }

void	Client::appendOutbuffer(const std::string& add) { _outBuffer += add; }

void	Client::eraseOutBuffer(size_t n) { _outBuffer.erase(0, n); }

bool	Client::hasOutput() const { return !_outBuffer.empty(); }
