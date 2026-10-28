# include "../headers/Channel.hpp"
# include "../headers/Client.hpp"
# include "../headers/Server.hpp"
# include <algorithm>
# include <sstream>

Channel::Channel(const std::string& name)
	: _name(name),
	_limit(0) {}

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

const std::string&	Channel::getModes() const { return _modes; }

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

bool	Channel::isBanned(const std::string& prefix) const
{
	std::vector<BanFormat>::const_iterator it;
	for (it = _banList.begin(); it < _banList.end(); ++it)
	{
		if (it->bannedPrefix == prefix)
			return true;
	}
	return false;
}

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

bool	Channel::isInviteOnly() { return hasMode('i'); }

bool	Channel::isSecret() { return hasMode('s'); }

bool	Channel::isNoExternalMsg() { return hasMode('n'); }

bool	Channel::isModerated() { return hasMode('m'); }

bool	Channel::isTopicOperatorOnly() { return hasMode('t'); }

// SETTERS
bool	Channel::setAdmin(int fd, bool oped)
{
	std::map<int, Member>::iterator	it = _members.find(fd);
	if (it == _members.end() || it->second.isAdmin == oped)
		return false;
	it->second.isAdmin = oped;
	return true;
}

bool	Channel::setVoiced(int fd, bool voiced)
{
	std::map<int, Member>::iterator	it = _members.find(fd);
	if (it == _members.end() || it->second.isVoiced == voiced)
		return false;
	it->second.isVoiced = voiced;
	return true;
}

void	Channel::setKey(const std::string& key) { _key = key; }

void	Channel::setLimit(size_t limit) { _limit = limit; }

void	Channel::setTopic(const std::string& topic) { _topic = topic; }

bool	Channel::setModes(char ch, bool add)
{
	size_t  pos = _modes.find_first_of(ch);

	if (add) {
		if (pos != std::string::npos)
			return false;
		_modes.append(std::string(1, ch));
		return true;
	}
	if (pos == std::string::npos)
		return false;
	_modes.erase(pos, 1);
	return true;
}

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

std::vector<BanFormat>::iterator	Channel::findBannedIterator(const std::string& bannedPrefix)
{
	std::vector<BanFormat>::iterator it = _banList.begin();
    
	while (it != _banList.end() && it->bannedPrefix != bannedPrefix)
        ++it;
    return it;
}

bool	Channel::banMember(const std::string& bannedPrefix, const std::string& authorPrefix)
{
	time_t	now;

	if (findBannedIterator(bannedPrefix) != _banList.end())
		return false;
	BanFormat	newBanned;
	newBanned.bannedPrefix = bannedPrefix;
	newBanned.authorPrefix = authorPrefix;
	newBanned.at = time(&now);
	_banList.push_back(newBanned);
	return true;
}

void	Channel::removeMember(int fd)
{
	_members.erase(fd);
}

bool	Channel::removeBan(const std::string& bannedPrefix)
{
	std::vector<BanFormat>::iterator it = findBannedIterator(bannedPrefix);

	if (it == _banList.end())
		return false;
	_banList.erase(it);
	return true;
}

std::string	Channel::formattedBan(BanFormat bf) const
{
	std::ostringstream	oss;

	oss << bf.bannedPrefix;
	oss << " ";
	oss << bf.authorPrefix;
	oss << " ";
	oss << bf.at;
	
	return oss.str();
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

bool	Channel::hasMode(char c) { return _modes.find_first_of(c) != std::string::npos; }
