#pragma once

# include <string>

/* TO IMPLEMENT

lipsesc coada de ieșire (_outBuffer) și flag-ul de închidere după trimitere	
adaugă queueOutput, getOutBuffer, eraseOutBuffer, markForClose

*/

// WE ARE DEFINING A MODEL OF CLASS USER
class User
{
	private:
		int			_socketFd;
		std::string	_nickname;
		std::string	_username;
		std::string _inputBuffer;
		bool        _hasPass;
		bool		_hasNick;
        bool		_hasUser;
		bool		_isAuth;

	public:
		// CONSTRUCTORS
		User();
		explicit User(int socketFd);

		// GETTERS
		int	getSocket() const;
		const std::string&	getNickname() const;
		const std::string&	getUsername() const;
		const std::string&	getBuffer() const;
		bool	getPassState() const;
        bool	getNickState() const;
        bool	getUserState() const;
		bool	getAuthState() const;

		// SETTERS
		void	setNickname(const std::string& nickname);
		void	setUsername(const std::string& username);
		void	setPassState(bool pass);
        void	setNickState(bool nick);
        void	setUserState(bool user);
        void	setAuthState(bool isAuth);
		// BUFFER MANIPULATION
		void	appendBuffer(const std::string& buf);
		void	eraseBuffer(size_t n);
		void	clearBuffer();
		

};
