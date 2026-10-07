#ifndef DISPATCHER_HPP
# define DISPATCHER_HPP

#include <map>
#include <string>

class Server;
class Client;
struct Message;

class Dispatcher
{
    public:
        typedef void(*Dispatch)(Server&, Client&, Message&);

    private:
        std::map<std::string, Dispatch>  dispatch;

        static void handleInvite(Server& sv, Client& cl, const Message& msg);
        static void handleJoin(Server& sv, Client& cl, const Message& msg);

    public:
        Dispatcher();
        ~Dispatcher();

        void    dispatch(Server& sv, Client& cl, const Message& msg);
};

#endif
