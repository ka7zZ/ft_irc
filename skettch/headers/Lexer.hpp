#ifndef LEXER_HPP
# define LEXER_HPP

# include <string>
# include <vector>
# include "Token.hpp"

class Lexer
{
	private:
		std::string	_src;
		size_t		_index;

		bool		isAtEnd() const;
		char		peek() const;
		char		advance();
		void		skipWhitespace();
		std::string	readUntil(const std::string& seq);

	public:
		explicit Lexer(const std::string& src);

		std::vector<Token>	tokenize();
};

#endif
