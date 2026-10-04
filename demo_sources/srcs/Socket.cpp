#include "../headers/Socket.hpp"

Socket::Socket(int socketFd)
    : fd(socketFd), registrationState(UNREGISTERED), user(NULL)
{
}

Socket::~Socket()
{
}

int Socket::getFd() const
{
    return fd;
}
