#ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include <map>
# include <string>

class User;

class Channel
{
    public:
        explicit Channel(const std::string &name);

        std::string name;
        std::map<User *, bool> members;
};

#endif
