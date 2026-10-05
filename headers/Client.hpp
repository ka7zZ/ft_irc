#ifndef CLIENT_HPP
# define CLIENT_HPP

# include <string>

// WE ARE DEFINING A MODEL OF CLASS USER
class Client
{
	private:
		int			_socketFd;
		std::string	_nickname;
		std::string	_username;
		std::string _hostname;
		std::string _inputBuffer;
		std::string _outBuffer;
		bool        _hasPass;
		bool		_hasNick;
		bool		_hasUser;
		bool		_isAuth;
		bool        _closeAfterFlush;

	public:
		// CONSTRUCTORS
		Client();
		explicit Client(int socketFd, const std::string& hostname);

		// GETTERS
		int	getSocket() const;
		const std::string&	getNickname() const;
		const std::string&	getUsername() const;
		const std::string&	getBuffer() const;
		const std::string&	getOutBuffer() const;
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

		// GETTING THE PREFIX WITH THE RIGHT FORMAT
		std::string prefix() const;

		// BUFFER MANIPULATION
		void	appendBuffer(const std::string& buf);
		void	eraseBuffer(size_t n);
		void	clearBuffer();
		void	appendOutbuffer(const std::string& add);
		void	eraseOutBuffer(size_t n);
		bool	hasOutput() const;

};

#endif