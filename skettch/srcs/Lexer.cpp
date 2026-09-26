# include "../headers/Lexer.hpp"

Lexer::Lexer(const std::string& src) : _src(src), _index(0) {}

bool	Lexer::isAtEnd() const
{
	return	_index >= _src.length();
}

char	Lexer::peek() const
{
	if (isAtEnd())
		return '\0';
	return _src[_index];
}

char	Lexer::advance()
{
	if (isAtEnd())
		return '\0';
	return _src[_index++];
}

void    Lexer::skipWhitespace()
{
	_index = _src.find_first_not_of(' ', _index);
	if (_index == std::string::npos)
		_index = _src.length();
}

std::string Lexer::readUntil(const std::string& seq)
{
	size_t		nextDelimiter;
	std::string	token;

	nextDelimiter = _src.find_first_of(seq, _index);
	if (nextDelimiter == std::string::npos)
		nextDelimiter = _src.length();
	token = _src.substr(_index, nextDelimiter - _index);
	_index = nextDelimiter;
	return token;
}

std::vector<Token>	Lexer::tokenize()
{
	std::vector<Token>	tokens;

	// 1. Check for Prefix
	if (!isAtEnd() && peek() == ':') {
		advance();
		Token tk;
		tk.type = PREFIX;
		tk.value = readUntil(" ");
		tokens.push_back(tk);
	}

	// Skip extra spaces
	skipWhitespace();

	// 2. Parse Command
	if (!isAtEnd() && peek() != '\r' && peek() != '\n')
	{
		std::string	cmdVal = readUntil(" \r\n");
		if (!cmdVal.empty())
		{
			Token tk;
			tk.type = COMMAND;
			tk.value = cmdVal;
			tokens.push_back(tk);
		}
	}

	// 3. Parse Parameters
	while (!isAtEnd())
	{
		skipWhitespace();
		if (isAtEnd() || peek() == '\r' || peek() == '\n')
			break;
		if (peek() == ':')
		{
			advance();
			size_t	start = _index;
			while (!isAtEnd() && peek() != '\r' && peek() != '\n')
				advance();
			Token tk;
			tk.type = TRAILING;
			tk.value = _src.substr(start, _index - start);
			tokens.push_back(tk);
			break;
		}
		else
		{
			std::string paramValue = readUntil(" \r\n");
			if (!paramValue.empty())
			{
				Token tk;
				tk.type = PARAMETER;
				tk.value = paramValue;
				tokens.push_back(tk);
			}
		}
	}
	Token endTk;
	endTk.type = END_OF_LINE;
	endTk.value = "";
	tokens.push_back(endTk);
	return tokens;
}
