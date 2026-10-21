# include "../headers/Channel.hpp"
# include "../headers/Client.hpp"
# include "../headers/Server.hpp"
# include <algorithm>
# include <sstream>

Channel::Channel(const std::string& name)
	: _name(name),
	_isInviteOnly(false),
	_isTopic(false),
	_isModerated(false),
	_isNoExternalMsg(false),
	_isSecret(false),
	_isPrivate(false) {}

// GETTERS
const std::string&	Channel::getName() const { return _name; }

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

	for (it = _members.begin(); it != _members.end(); ++it) {
		if (it->second.cl->getNickname() == nickname)
			return it->second.cl;
	}
	return NULL;
}

const std::map<int, Member>&	Channel::getAllMembers() const { return _members; }

const std::vector<BanFormat>& Channel::getBanList() const { return _banList; }

const std::string&	Channel::getKey() const { return _key; }

size_t	Channel::getLimit() const { return _limit; }

const std::string&	Channel::getTopic() const { return _topic; }

size_t	Channel::getChannelSize() const { return _members.size(); }

const std::string&	Channel::getChannelMode() const
{
	std::ostringstream	oss;
	bool				isLimit = false;
	bool				isKey = false;
	if (isInviteOnly())
		oss << "i";
	if (!_key.empty()) {
		isKey = true;
		oss << "k";
	}
	if (_limit > 0) {
		isLimit = true;
		oss << "l";
	}
	if (isModerated())
		oss << "m";
	if (isNoExternalMsg())
		oss << "n";
	if (isPrivate())
		oss << "p";
	if (isSecret())
		oss << "s";
	if (isTopic())
		oss << "t";
	if (isKey)
		oss << " " << _key;
	if (isLimit)
		oss << " " << _limit;
	return oss.str();
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

bool	Channel::isVoiced(int fd) const
{
	std::map<int, Member>::const_iterator	it = _members.find(fd);

	return it != _members.end() && it->second.isVoiced;
}

bool	Channel::isBanned(const std::string& nick) const
{
	for (size_t i = 0; i < _banList.size(); ++i)
	{
		if (_banList[i] == nick)
			return true;
	}
	return false;
}

bool	Channel::isInviteOnly() const { return _isInviteOnly; }

bool	Channel::isTopic() const { return _isTopic; }

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

bool	Channel::isModerated() const { return _isModerated; }

bool	Channel::isNoExternalMsg() const { return _isNoExternalMsg; }

bool	Channel::isSecret() const { return _isSecret; }

bool	Channel::isPrivate() const { return _isPrivate; }

// SETTERS
void	Channel::setAdmin(int fd)
{
	std::map<int, Member>::iterator	it = _members.find(fd);
	if (it != _members.end())
		it->second.isAdmin = true;
}

void	Channel::setVoiced(int fd, bool isVoiced)
{
	std::map<int, Member>::iterator	it = _members.find(fd);
	if (it != _members.end())
		it->second.isVoiced = true;
}

void	Channel::setKey(const std::string& key) { _key = key; }

void	Channel::setLimit(size_t limit) { _limit = limit; }

void	Channel::setTopic(const std::string& topic) { _topic = topic; }

void	Channel::setInviteOnly(bool inviteOnly) { _isInviteOnly = inviteOnly; }

void	Channel::setTopicRestricted(bool topicRestricted) { _isTopic = topicRestricted; }

void	Channel::setModerated(bool isModerated) { _isModerated = isModerated; }

void	Channel::setNoExternalMsg(bool isNoExternalMsg) { _isNoExternalMsg = isNoExternalMsg; }

void	Channel::setSecret(bool isSecret) { _isSecret = isSecret; }

void	Channel::setPrivate(bool isPrivate) { _isPrivate = isPrivate; }

// CHANNEL UTILS
void	Channel::addMember(Client* cl, bool isAdmin, bool isVoiced)
{
	if (cl == NULL)
		return ;
	Member newMember;
	newMember.cl = cl;
	newMember.isAdmin = isAdmin;
	newMember.isVoiced = isVoiced;
	_members[cl->getSocket()] = newMember;

}

BanFormat*	Channel::findBanned(const std::string& bannedPrefix)
{
	std::vector<BanFormat>::iterator it;

	for (it = _banList.begin(); it != _banList.end(); ++it) {
		
		if (it->bannedPrefix == bannedPrefix)
			return &(*it);
	}
	return NULL;
}

void	Channel::banMember(const std::string& bannedPrefix, const std::string& authorPrefix)
{
	time_t	now;

	if (findBanned(bannedPrefix))
		return ;
	BanFormat	newBanned;
	newBanned.bannedPrefix = bannedPrefix;
	newBanned.authorPrefix = authorPrefix;
	newBanned.at = time(&now);
	_banList.push_back(newBanned);
}

void	Channel::removeMember(int fd)
{
	_members.erase(fd);
}

void	Channel::removeBan(const std::string& bannedPrefix)
{
	std::vector<BanFormat>::iterator it;

	for (it = _banList.begin(); it != _banList.end(); ++it) {
		
		if (it->bannedPrefix == bannedPrefix) {
			_banList.erase(it);
			return ;
		}
	}
}

void	Channel::addInvited(int fd)
{
	if (isInvited(fd))
		return ;
	_invited.push_back(fd);
}

void	Channel::removeInvited(int fd)
{
	std::vector<int>::iterator	it = std::find(_invited.begin(), _invited.end(), fd);
	
	if (it != _invited.end())
		_invited.erase(it);
}
