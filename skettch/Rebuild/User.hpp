#pragma once

# include <string>

// WE ARE DEFINING A MODEL OF CLASS USER
class User
{
	private:
		int			_socketFd;
		std::string	_nickname;
		std::string	_username;
		std::string	_realname;
		std::string _inputBuffer;
		bool		_isAuth;

	public:
		// CONSTRUCTORS
		User();
		explicit User(int socketFd);

		// GETTERS
		int	getSocket() const;
		const std::string&	getNickname() const;
		const std::string&	getUsername() const;
		const std::string&	getRealname() const;
		const std::string&	getBuffer() const;
		bool	isAuth() const;
		// SETTERS
		void	setNickname(const std::string& nickname);
		void	setUsername(const std::string& username);
		void	setRealname(const std::string& realname);
		void	setAuthenticated(bool auth);
		// BUFFER MANIPULATION
		void	appendBuffer(const std::string& buf);
		void	eraseBuffer(size_t n);
		void	clearBuffer();
		

};
