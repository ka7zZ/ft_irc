#ifndef USER_HPP
# define USER_HPP

#include <string>
#include "ISocket.hpp"

class User {

	private:
		int			_id;
		std::string	_nickname;
		std::string	_username;
		std::string	_realname;
		std::string	_hostname;
		ISocket*	_socket;
		bool		_isAuth;

	public:
		User(int id, ISocket* socket);
		~User();

		// GETTERS
		int	getId() const;
		const std::string&	getNickname() const;
		const std::string&	getUsername() const;
		const std::string&	getRealname() const;
		const std::string&	getHostname() const;
		ISocket*	getSocket() const;
		bool	isAuth() const;

		// SETTERS
		void	setNickname(const std::string& nickname);
		void	setUsername(const std::string& username);
		void	setRealname(const std::string& realname);
		void	setHostname(const std::string& hostname);
		void	setAuthenticated(bool auth);
};

#endif
