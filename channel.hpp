#pragma once

#include "irc.hpp"

//int -> {Client* ,  bool }

typedef struct  s_Member{
	Client*	cl;
	bool	isAdmin;	
} Member;

class Channel
{
	private:
		std::string name;
		std::map<int , Member> members;
		bool inviteOnly;          // mode i
		bool topicRestricted;     // mode t
		bool hasKey;              // mode k
		std::string key;          // mode k's value
		bool hasLimit;            // mode l
		size_t limit;               // mode l's value
		std::string topic;
	
	public:

		Channel();
		Channel(std::string name);
		Channel(const Channel&);
		Channel& operator=(const Channel&);



		// setter
		
		std::map<int , Member>& get_members();
		
};
