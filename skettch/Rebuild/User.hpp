#pragma once

# include <string>

// WE ARE DEFINING A MODEL OF CLASS USER
class User
{
	private:
		int			_socketFd;
		std::string	_nickname;
		std::string	_username;
		std::string	_password;
		std::string _inputBuffer;
		bool        _passOk;
		bool		_isNamed;
		bool		_isAuth;

	public:
		// CONSTRUCTORS
		User();
		explicit User(int socketFd);

		// GETTERS
		int	getSocket() const;
		const std::string&	getNickname() const;
		const std::string&	getUsername() const;
		const std::string&	getPassword() const;
		const std::string&	getBuffer() const;
		bool	passOk() const;
		bool	isNamed() const;
		bool	isAuth() const;

		// SETTERS
		void	setNickname(const std::string& nickname);
		void	setUsername(const std::string& username);
		void	setPassword(const std::string& password);
		void	setAuth(bool isAuth);
		void	setNamed(bool isNamed) const;
		void	setPassOk(bool pass);
		// BUFFER MANIPULATION
		void	appendBuffer(const std::string& buf);
		void	eraseBuffer(size_t n);
		void	clearBuffer();
		

};
