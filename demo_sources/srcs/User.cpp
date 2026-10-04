#include "../headers/User.hpp"

User::User(const std::string &userNick, const std::string &userName,
           const std::string &userRealname, Socket *userSocket)
    : nick(userNick), username(userName), realname(userRealname),
      socket(userSocket)
{
}
