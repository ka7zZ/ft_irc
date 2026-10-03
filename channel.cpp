#include "irc.hpp"

Channel::Channel()
{}

Channel::Channel(std::string _name)
{
	name = _name;
	inviteOnly = false;
	topicRestricted = false;
	hasKey = false;
	hasLimit = false;
}

Channel::Channel(const Channel& other)
{
	name = other.name;
	members = other.members;
	inviteOnly = other.inviteOnly;
	topicRestricted = other.topicRestricted;
	hasKey = other.hasKey;
	key = other.key;
	hasLimit = other.hasLimit;
	topic = other.topic;

}


Channel& Channel::operator=(const Channel& other)
{
	if (this != &other)
	{
		name = other.name;
		members = other.members;
		inviteOnly = other.inviteOnly;
		topicRestricted = other.topicRestricted;
		hasKey = other.hasKey;
		key = other.key;
		hasLimit = other.hasLimit;
		topic = other.topicRestricted;
	}
	return (*this);
}


std::map<int , Member>& Channel::get_members()
{
	return (members);
}
