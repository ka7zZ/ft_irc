#ifndef CHANNEL_HPP
# define CHANNEL_HPP

#include <string>
#include <map>
#include <vector>

class Client;
	
struct  Member{
	Client*	cl;
	bool	isAdmin;
};

class Channel
{
	private:
		std::string 			_name;
		std::map<int , Member>	_members;
		std::vector<int>		_invited;
		bool 					_inviteOnly;
		bool 					_topicRestricted;
		std::string				_key;
		size_t					_limit;
		std::string				_topic;

	public:

		explicit	Channel(const std::string& name);

		const std::string&			getName() const;
		const std::string&			getTopic() const;
		const std::string&			getKey() const;
		int							getInvitedFd() const;
		size_t						getLimit() const;
		Client*						getMember(int memberFd) const;
		Client*						getMemberByNickname(const std::string& nickname) const;
		const std::map<int, Member>	getAll() const;
		size_t						getSize() const;

		void						setTopic(const std::string& topic);
		void						setKey(const std::string& key);
		void						setInviteOnly(bool inviteOnly);
		void						setTopicRestricted(bool topicRestricted);
		void						setLimit(size_t limit);

		void						addMember(Client* cl, bool isAdmin);
		void						removeMember(int fd);
		void						addInvited(int fd);
		void						removeInvited(int fd);
		void						setAdmin(int fd, bool isAdmin);

		bool						isMember(int fd) const;
		bool						isAdmin(int fd) const;
		bool						isInviteOnly() const;
		bool						isTopicRestricted() const;
		bool						isInvited(int fd) const;

};

#endif