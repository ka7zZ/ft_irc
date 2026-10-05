#ifndef IRCERROR_HPP
# define IRCERROR_HPP

# include <exception>
# include <string>
# include <sstream>

class IRCError: public std::exception {

	private:
		int	_code;
		std::string	_arg;

	public:
		IRCError(int code, const std::string& arg);
		virtual ~IRCError()	throw();

        // CONSTRUCT THE ERROR STRING
		std::string			format(const std::string& arg) const;
};

#endif