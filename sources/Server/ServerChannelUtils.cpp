# include "../../headers/Server.hpp"

void	Server::createChannel(Client* cl, const std::string& name)
{
	std::map<std::string, Channel>::const_iterator  it;
	const std::string	normalized = normalizeString(name);

	for (it = _channels.begin(); it != _channels.end(); it++)
	{
		if (normalizeString(it->second.getName()) == normalized)
			return ;
	}

	Channel ch(name);

	ch.addMember(cl, true, true);
	_channels.insert(std::make_pair(name, ch));
}

void	Server::removeChannel(Channel *ch)
{
	std::map<std::string, Channel>::iterator	it = _channels.find(ch->getName());

	if (it != _channels.end())
		_channels.erase(it);
}

Channel*    Server::getChannel(const std::string& channelName)
{
	std::map<std::string, Channel>::iterator  it;
	const std::string	normalized = normalizeString(channelName);

	for (it = _channels.begin(); it != _channels.end(); it++)
	{
		if (normalizeString(it->second.getName()) == normalized)
			return &it->second;
	}
	return NULL;
}

const std::map<std::string, Channel>&	Server::getAllChannels() const { return _channels; }


