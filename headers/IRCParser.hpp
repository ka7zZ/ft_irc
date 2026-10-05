#ifndef IRCPARSER_HPP
# define IRCPARSER_HPP

/* CHANGED THE INCLUDE - OPTIMIZATION MEMORY:

If we include the irc.hpp:
	- will upload all the includes
	- bu we need only libraries that we are using
*/

# include <string>
# include <vector>
# include <cstddef>

// ENUM FOR TOKEN TYPE
enum TokenType
{
	PREFIX,
	COMMAND,
	PARAMETER,
	TRAILING,
	END_OF_LINE
};

// TOKEN CONTENT
struct Token
{
	TokenType   type;
	std::string value;
};

// PARAMETER CONTENT
struct Parameter {
	std::string value;
	bool        isTrailling;
};

// RETURNED MESSAGE FORMAT
struct Message {
	std::string 			prefix;
	std::string 			command;
	std::vector<Parameter>  params;
};

// CLASS PARSER (LEXER + FORMAT)
class IRCParser
{
	private:
		size_t				_index;
		std::vector<Token>	_tokens;

		// TOKENIZER + UTILS
		
		bool			endOfSource(const std::string& src) const;
		char			currentChar(const std::string& src) const;
		char			advanceChar(const std::string& src);
		void			skipWhitespaces(const std::string& src);
		std::string		appendUntil(const std::string& src, const std::string& seq);
		void			addToken(TokenType type, std::string value);
		void			tokenizer(const std::string&);

	public:
		explicit IRCParser(const std::string& src);
		
		Message			parseTokens();
};

#endif