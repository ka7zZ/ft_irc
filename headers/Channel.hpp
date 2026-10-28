#ifndef CHANNEL_HPP
# define CHANNEL_HPP

#include <string>
#include <map>
#include <vector>
#include <ctime>

class Client;

struct  Member{
	Client*	cl;
	bool	isAdmin;
	bool	isVoiced;
};

struct	BanFormat
{
	std::string	bannedPrefix;
	std::string	authorPrefix;
	time_t		at;
};

class Channel
{
	private:
		std::string 				_name;
		std::map<int, Member>		_members;
		std::vector<int>			_invited;
		std::vector<BanFormat>		_banList;			// +b
		std::string					_key;				// +k
		size_t						_limit;				// +l
		std::string					_topic;
		std::string					_modes;				// +imnstp

	public:

		explicit	Channel(const std::string& name);

		// GETTERS
		const std::string&					getName() const;
		Client*								getMember(int memberFd) const;
		Client*								getMemberByNickname(const std::string& nickname) const;
		const std::map<int, Member>&		getAllMembers() const;
		const std::vector<BanFormat>&		getBanList() const;
		const std::string&					getKey() const;
		size_t								getLimit() const;
		const std::string&					getTopic() const;
		size_t								getChannelSize() const;
		const std::string&					getModes() const;
		bool								isMember(int fd) const;
		bool								isAdmin(int fd) const;
		bool 								isVoiced(int fd) const;
		bool 								isBanned(const std::string& prefix) const;
		bool								isInvited(int fd) const;
		bool								isInviteOnly();
		bool								isSecret();
		bool								isNoExternalMsg();
		bool								isModerated();
		bool								isTopicOperatorOnly();
		
		// SETTERS
		bool								setAdmin(int fd, bool oped);
		bool								setVoiced(int fd, bool voiced);
		void								setKey(const std::string& key);
		void								setLimit(size_t limit);
		void								setTopic(const std::string& topic);
		bool								setModes(char c, bool add);
		
		// CHANNEL UTILS
		void								addMember(Client* cl, bool isAdmin, bool isVoiced);
		void								removeMember(int fd);
		std::vector<BanFormat>::iterator	findBannedIterator(const std::string& bannedPrefix);
		bool								banMember(const std::string& bannedPrefix, const std::string& authorPrefix);
		bool								removeBan(const std::string& bannedPrefix);
		std::string							formattedBan(BanFormat bf) const;
		void								addInvited(int fd);
		void								removeInvited(int fd);
		bool								hasMode(char c);

};

#endif
