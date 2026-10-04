#ifndef SERVER_HPP
# define SERVER_HPP

# include <map>
# include <string>

# include "Channel.hpp"
# include "Socket.hpp"
# include "User.hpp"

class Server
{
    private:
        int listenerFd;
        std::map<int, Socket *> sockets;
        std::map<std::string, User *> users;
        std::map<std::string, Channel *> channels;

        void acceptClients(void);
        void readFrom(Socket *socket);
        void writeTo(Socket *socket);
        void processLine(Socket *socket, const std::string &line);
        void handleNick(Socket *socket, const std::string &nick);
        void handleUser(Socket *socket, const std::string &username,
                        const std::string &realname);
        void handleJoin(Socket *socket, const std::string &channelName);
        void finishRegistration(Socket *socket);
        void send(Socket *socket, const std::string &message);
        void closeClient(int fd);
        static std::string channelMembers(Channel *channel);
        static std::string normalize(const std::string &value);
        static bool validNick(const std::string &nick);
        static bool validChannel(const std::string &name);

    public:
        explicit Server(int port);
        ~Server();

        void run(void);
};

#endif