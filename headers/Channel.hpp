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
		bool 						_isInviteOnly;		// +i
		bool 						_isTopic;			// +t
		bool						_isModerated;		// +m
		bool						_isNoExternalMsg;	// +n
		bool						_isSecret;			// +s
		bool						_isPrivate;			// +p

	public:

		explicit	Channel(const std::string& name);

		// GETTERS
		const std::string&				getName() const;
		Client*							getMember(int memberFd) const;
		Client*							getMemberByNickname(const std::string& nickname) const;
		const std::map<int, Member>&	getAllMembers() const;
		const std::vector<BanFormat>&	getBanList() const;
		const std::string&				getKey() const;
		size_t							getLimit() const;
		const std::string&				getTopic() const;
		size_t							getChannelSize() const;
		const std::string&				getChannelMode() const;
		bool							isMember(int fd) const;
		bool							isAdmin(int fd) const;
		bool 							isVoiced(int fd) const;
		bool 							isBanned(const std::string& nick) const;
		bool							isInvited(int fd) const;
		bool							isInviteOnly() const;
		bool							isTopic() const;
		bool							isModerated() const;
		bool							isNoExternalMsg() const;
		bool							isSecret() const;
		bool							isPrivate() const;
		
		// SETTERS
		void							setAdmin(int fd);
		void							setVoiced(int fd, bool isVoiced);
		void							setKey(const std::string& key);
		void							setLimit(size_t limit);
		void							setTopic(const std::string& topic);
		void							setInviteOnly(bool inviteOnly);
		void							setTopicRestricted(bool topicRestricted);
		void							setModerated(bool isModerated);
		void							setNoExternalMsg(bool isNoExternalMsg);
		void							setSecret(bool isSecret);
		void							setPrivate(bool isPrivate);
		void							setVoiced(int fd, bool isVoiced);
		
		// CHANNEL UTILS
		void							addMember(Client* cl, bool isAdmin, bool isVoiced);
		void							removeMember(int fd);
		BanFormat*						findBanned(const std::string& bannedPrefix);
		void							banMember(const std::string& bannedPrefix, const std::string& authorPrefix);
		void							removeBan(const std::string& bannedPrefix);
		void							addInvited(int fd);
		void							removeInvited(int fd);
};

#endif
