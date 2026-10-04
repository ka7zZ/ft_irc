#include "../headers/Server.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace
{
    std::vector<std::string> splitParameters(const std::string &line)
    {
        std::vector<std::string> parameters;
        std::string::size_type position = 0;

        while (position < line.size())
        {
            while (position < line.size() && line[position] == ' ')
                ++position;
            if (position == line.size())
                break;
            if (line[position] == ':')
            {
                parameters.push_back(line.substr(position + 1));
                break;
            }
            std::string::size_type end = line.find(' ', position);
            if (end == std::string::npos)
                end = line.size();
            parameters.push_back(line.substr(position, end - position));
            position = end;
        }
        return parameters;
    }

    bool isLetter(char character)
    {
        return std::isalpha(static_cast<unsigned char>(character)) != 0;
    }

    bool isNickCharacter(char character)
    {
        return std::isalnum(static_cast<unsigned char>(character)) != 0
            || character == '-' || character == '[' || character == ']'
            || character == '\\' || character == '`' || character == '^'
            || character == '{' || character == '}' || character == '|';
    }
}

Server::Server(int port) : listenerFd(-1)
{
    listenerFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenerFd < 0)
        throw std::runtime_error("socket failed");

    int reuseAddress = 1;
    if (setsockopt(listenerFd, SOL_SOCKET, SO_REUSEADDR,
                   &reuseAddress, sizeof(reuseAddress)) < 0)
        throw std::runtime_error("setsockopt failed");

    int flags = fcntl(listenerFd, F_GETFL, 0);
    if (flags < 0 || fcntl(listenerFd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl failed");

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(listenerFd, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0 || listen(listenerFd, 16) < 0)
        throw std::runtime_error("bind/listen failed");
}

Server::~Server()
{
    while (!sockets.empty())
        closeClient(sockets.begin()->first);
    for (std::map<std::string, User *>::iterator it = users.begin();
         it != users.end(); ++it)
        delete it->second;
    for (std::map<std::string, Channel *>::iterator it = channels.begin();
         it != channels.end(); ++it)
        delete it->second;
    if (listenerFd >= 0)
        close(listenerFd);
}

void Server::run(void)
{
    while (true)
    {
        std::vector<pollfd> descriptors;
        pollfd listener;
        listener.fd = listenerFd;
        listener.events = POLLIN;
        listener.revents = 0;
        descriptors.push_back(listener);

        for (std::map<int, Socket *>::const_iterator it = sockets.begin();
             it != sockets.end(); ++it)
        {
            pollfd descriptor;
            descriptor.fd = it->first;
            descriptor.events = POLLIN;
            if (!it->second->outputBuffer.empty())
                descriptor.events |= POLLOUT;
            descriptor.revents = 0;
            descriptors.push_back(descriptor);
        }

        if (poll(&descriptors[0], descriptors.size(), -1) < 0)
        {
            if (errno == EINTR)
                continue;
            throw std::runtime_error("poll failed");
        }

        if (descriptors[0].revents & POLLIN)
            acceptClients();

        for (std::size_t index = 1; index < descriptors.size(); ++index)
        {
            int fd = descriptors[index].fd;
            short events = descriptors[index].revents;
            if (sockets.find(fd) == sockets.end())
                continue;
            if (events & (POLLERR | POLLHUP | POLLNVAL))
            {
                closeClient(fd);
                continue;
            }
            if (events & POLLIN)
                readFrom(sockets[fd]);
            if (sockets.find(fd) != sockets.end() && (events & POLLOUT))
                writeTo(sockets[fd]);
        }
    }
}

void Server::acceptClients(void)
{
    while (true)
    {
        int fd = accept(listenerFd, NULL, NULL);
        if (fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            return;
        }
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            close(fd);
            continue;
        }
        sockets[fd] = new Socket(fd);
    }
}

void Server::readFrom(Socket *socket)
{
    char buffer[4096];
    while (true)
    {
        ssize_t count = recv(socket->fd, buffer, sizeof(buffer), 0);
        if (count == 0)
        {
            closeClient(socket->fd);
            return;
        }
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            closeClient(socket->fd);
            return;
        }
        socket->inputBuffer.append(buffer, count);
        if (socket->inputBuffer.size() > 4096)
        {
            closeClient(socket->fd);
            return;
        }

        std::string::size_type end;
        while ((end = socket->inputBuffer.find("\r\n")) != std::string::npos)
        {
            std::string line = socket->inputBuffer.substr(0, end);
            socket->inputBuffer.erase(0, end + 2);
            if (line.size() + 2 > 512)
            {
                closeClient(socket->fd);
                return;
            }
            processLine(socket, line);
            if (sockets.find(socket->fd) == sockets.end())
                return;
        }
    }
}

void Server::writeTo(Socket *socket)
{
    while (!socket->outputBuffer.empty())
    {
        ssize_t count = ::send(socket->fd, socket->outputBuffer.data(),
                       socket->outputBuffer.size(), MSG_NOSIGNAL);
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            closeClient(socket->fd);
            return;
        }
        socket->outputBuffer.erase(0, count);
    }
}

void Server::processLine(Socket *socket, const std::string &line)
{
    std::vector<std::string> words = splitParameters(line);
    if (words.empty())
        return;

    std::string command = words[0];
    for (std::size_t i = 0; i < command.size(); ++i)
        command[i] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(command[i])));
    words.erase(words.begin());

    if (command == "NICK")
    {
        if (words.size() != 1)
            send(socket, ":irc.local 431 * :No nickname given\r\n");
        else
            handleNick(socket, words[0]);
    }
    else if (command == "USER")
    {
        if (words.size() != 4)
            send(socket, ":irc.local 461 * USER :Not enough parameters\r\n");
        else
            handleUser(socket, words[0], words[3]);
    }
    else if (command == "JOIN")
    {
        if (words.size() != 1)
            send(socket, ":irc.local 461 * JOIN :Not enough parameters\r\n");
        else
            handleJoin(socket, words[0]);
    }
    else if (command == "PING")
        send(socket, ":irc.local PONG irc.local\r\n");
    else
        send(socket, ":irc.local 421 * " + command + " :Unknown command\r\n");
}

void Server::handleNick(Socket *socket, const std::string &newNick)
{
    std::string key = normalize(newNick);
    if (!validNick(newNick))
    {
        send(socket, ":irc.local 432 * " + newNick + " :Erroneous nickname\r\n");
        return;
    }
    if (users.find(key) != users.end())
    {
        send(socket, ":irc.local 433 * " + newNick + " :Nickname is already in use\r\n");
        return;
    }
    socket->nick = newNick;
    socket->registrationState = Socket::NICK_SEEN;
    finishRegistration(socket);
}

void Server::handleUser(Socket *socket, const std::string &username,
                        const std::string &realname)
{
    if (socket->registrationState == Socket::REGISTERED)
    {
        send(socket, ":irc.local 462 " + socket->nick
             + " :You may not reregister\r\n");
        return;
    }
    socket->username = username;
    socket->realname = realname;
    finishRegistration(socket);
}

void Server::finishRegistration(Socket *socket)
{
    if (socket->registrationState != Socket::NICK_SEEN
        || socket->username.empty())
        return;

    User *user = new User(socket->nick, socket->username,
                          socket->realname, socket);
    users[normalize(user->nick)] = user;
    socket->user = user;
    socket->registrationState = Socket::REGISTERED;
    send(socket, ":irc.local 001 " + user->nick
         + " :Welcome to the IRC demo\r\n");
    send(socket, ":irc.local 002 " + user->nick
         + " :Your host is irc.local\r\n");
    send(socket, ":irc.local 003 " + user->nick
         + " :This server was created today\r\n");
    send(socket, ":irc.local 004 " + user->nick
         + " irc.local demo oi\r\n");
}

void Server::handleJoin(Socket *socket, const std::string &channelName)
{
    if (socket->registrationState != Socket::REGISTERED)
    {
        send(socket, ":irc.local 451 * :You have not registered\r\n");
        return;
    }
    if (!validChannel(channelName))
    {
        send(socket, ":irc.local 403 " + socket->nick + " "
             + channelName + " :No such channel\r\n");
        return;
    }

    std::string key = normalize(channelName);
    Channel *channel;
    bool creator = false;
    if (channels.find(key) == channels.end())
    {
        channel = new Channel(channelName);
        channels[key] = channel;
        creator = true;
    }
    else
        channel = channels[key];

    if (channel->members.find(socket->user) != channel->members.end())
        return;

    channel->members[socket->user] = creator;
    socket->user->channels.insert(channel);

    std::string joinMessage = ":" + socket->nick + "!"
        + socket->username + "@localhost JOIN " + channel->name + "\r\n";
    for (std::map<User *, bool>::iterator it = channel->members.begin();
         it != channel->members.end(); ++it)
        send(it->first->socket, joinMessage);

    send(socket, ":irc.local 353 " + socket->nick + " = "
         + channel->name + " :" + channelMembers(channel) + "\r\n");
    send(socket, ":irc.local 366 " + socket->nick + " "
         + channel->name + " :End of /NAMES list\r\n");
}

std::string Server::channelMembers(Channel *channel)
{
    std::string result;
    for (std::map<User *, bool>::iterator it = channel->members.begin();
         it != channel->members.end(); ++it)
    {
        if (!result.empty())
            result += " ";
        if (it->second)
            result += "@";
        result += it->first->nick;
    }
    return result;
}

void Server::send(Socket *socket, const std::string &message)
{
    socket->outputBuffer += message;
}

void Server::closeClient(int fd)
{
    std::map<int, Socket *>::iterator socketIt = sockets.find(fd);
    if (socketIt == sockets.end())
        return;
    Socket *socket = socketIt->second;
    if (socket->user != NULL)
    {
        for (std::set<Channel *>::iterator it = socket->user->channels.begin();
             it != socket->user->channels.end(); ++it)
            (*it)->members.erase(socket->user);
        users.erase(normalize(socket->user->nick));
        delete socket->user;
    }
    sockets.erase(socketIt);
    close(fd);
    delete socket;
}

std::string Server::normalize(const std::string &value)
{
    std::string result = value;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<char>(std::tolower(
            static_cast<unsigned char>(result[i])));
    return result;
}

bool Server::validNick(const std::string &nick)
{
    if (nick.empty() || nick.size() > 9 || !isLetter(nick[0]))
        return false;
    for (std::size_t i = 1; i < nick.size(); ++i)
    {
        if (!isNickCharacter(nick[i]))
            return false;
    }
    return true;
}

bool Server::validChannel(const std::string &name)
{
    if (name.empty() || name.size() > 50
        || (name[0] != '#' && name[0] != '&'))
        return false;
    for (std::size_t i = 1; i < name.size(); ++i)
    {
        if (name[i] == ' ' || name[i] == ',' || name[i] == '\a'
            || name[i] == '\r' || name[i] == '\n')
            return false;
    }
    return true;
}
