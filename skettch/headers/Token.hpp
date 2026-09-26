#ifndef TOKEN_HPP
# define TOKEN_HPP

#include <string>
enum TokenType
{
	PREFIX,
	COMMAND,
	PARAMETER,
	TRAILING,
	END_OF_LINE
};

struct Token
{
	TokenType   type;
	std::string value;
};

#endif