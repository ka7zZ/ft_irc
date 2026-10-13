# include "../headers/Channel.hpp"
# include "../headers/Client.hpp"

Channel::Channel(const std::string& name)
	: _name(name), 
	_inviteOnly(false),
	_topicRestricted(false),
	_key(""),
	_limit(0),
	_topic("") {}

const std::string&	Channel::getName() const { return _name; }

const std::string&	Channel::getTopic() const { return _topic; }

const std::string&	Channel::getKey() const { return _key; }

size_t	Channel::getLimit() const { return _limit; }

Client*	Channel::getMember(int memberFd) const
{
	std::map<int, Member>::const_iterator	it = _members.find(memberFd);

	if (it == _members.end())
		return NULL;
	return it->second.cl;
}

Client*	Channel::getMemberByNickname(const std::string& nickname) const
{
	std::map<int, Member>::const_iterator	it;

	for (it = _members.begin(); it != _members.end(); it++) {
		if (it->second.cl->getNickname() == nickname)
			return it->second.cl;
	}
	return NULL;
}

const std::map<int, Member>&	Channel::getAll() const { return _members; }

size_t	Channel::getSize() const { return _members.size(); }

void    Channel::setTopic(const std::string& topic) { _topic = topic; }

void	Channel::setKey(const std::string& key) { _key = key; }

void	Channel::setInviteOnly(bool inviteOnly) { _inviteOnly = inviteOnly; }

void	Channel::setTopicRestricted(bool topicRestricted) { _topicRestricted = topicRestricted; }

void	Channel::setLimit(size_t limit) { _limit = limit; }

void	Channel::addMember(Client* cl, bool isAdmin)
{
	if (cl == NULL)
		return ;
	Member newMember;
	newMember.cl = cl;
	newMember.isAdmin = isAdmin;
	_members.insert(std::make_pair(cl->getSocket(), newMember));

}

void	Channel::removeMember(int fd)
{
	_members.erase(fd);
}

void	Channel::addInvited(int fd)
{
	if (isInvited(fd))
		return ;
	_invited.push_back(fd);
}

void	Channel::removeInvited(int fd)
{
	std::vector<int>::iterator	it;
	
	for (it = _invited.begin(); it != _invited.end(); it++)
	{
		if (*it == fd) {
			_invited.erase(it);
			return ;
		}
	}
}

void	Channel::setAdmin(int fd, bool isAdmin)
{
	std::map<int, Member>::iterator it = _members.find(fd);
	if (it != _members.end())
		it->second.isAdmin = isAdmin;
}

bool	Channel::isMember(int fd) const
{
	std::map<int, Member>::const_iterator	it = _members.find(fd);

	return it != _members.end();
}

bool	Channel::isAdmin(int fd) const
{
	std::map<int, Member>::const_iterator	it = _members.find(fd);

	return it != _members.end() && it->second.isAdmin;
}

bool	Channel::isInviteOnly() const { return _inviteOnly; }

bool	Channel::isTopicRestricted() const { return _topicRestricted; }

bool	Channel::isInvited(int fd) const
{
	std::vector<int>::const_iterator	it;
	for (it = _invited.begin(); it != _invited.end(); it++)
	{
		if (*it == fd)
			return true;
	}
	return false;
}