#include "IRCParser.hpp"

IRCParser::IRCParser(const std::string& src) {
	_index = 0;
	tokenizer(src);
}

// TOKENIZER + UTILS
bool	IRCParser::endOfSource(const std::string& src) const
{
	return _index >= src.length();
}

char	IRCParser::currentChar(const std::string& src) const
{
	return src[_index];
}

char	IRCParser::advanceChar(const std::string& src)
{
	if (endOfSource(src))
		return '\0';
	return src[++_index];
}

void	IRCParser::skipWhitespaces(const std::string& src)
{
	_index = src.find_first_not_of(' ', _index);
	if (_index == std::string::npos)
		_index = src.length();
}

std::string	IRCParser::appendUntil(const std::string& src, const std::string& seq)
{
	size_t	nextDelimiter = src.find_first_of(seq, _index);
	if (nextDelimiter == std::string::npos)
		nextDelimiter = src.length();
	std::string	value = src.substr(_index, nextDelimiter - _index);
	_index = nextDelimiter;
	return value;
}

void	IRCParser::addToken(TokenType type, std::string value) {
	Token tk;

	tk.type = type;
	tk.value = value;
	_tokens.push_back(tk);
}

void	IRCParser::tokenizer(const std::string& src)
{

	skipWhitespaces(src);
	if (!endOfSource(src) && currentChar(src) == ':')
	{
		advanceChar(src);
		addToken(PREFIX, appendUntil(src, " "));
	}
	skipWhitespaces(src);
	if (!endOfSource(src) && currentChar(src) != '\n')
	{
		std::string cmd = appendUntil(src, " \r\n");
		if (!cmd.empty())
			addToken(COMMAND, cmd);
	}
	while (!endOfSource(src))
	{
		skipWhitespaces(src);
		if (endOfSource(src) || currentChar(src) == '\r' || currentChar(src) == '\n')
			break ;
		if (currentChar(src) == ':')
		{
			advanceChar(src);
			size_t start = _index;
			while (!endOfSource(src) && currentChar(src) != '\r' && currentChar(src) != '\n' )
				advanceChar(src);
			addToken(TRAILING, src.substr(start, _index - start));
			break ;
		}
		std::string param = appendUntil(src, " \r\n");
		if (!param.empty())
			addToken(PARAMETER, param);
	}
	addToken(END_OF_LINE, "");
}

// PARSER + UTILS
Message	IRCParser::parseTokens()
{
	Message	parsedMessage;
	std::vector<Token>::iterator	it;

    it = _tokens.begin();
	if (it->type == PREFIX)
        parsedMessage.prefix = (it++)->value;
	if (it->type == COMMAND)
        parsedMessage.command = (it++)->value;
	while (it->type == PARAMETER || it->type == TRAILING)
	{
		Parameter p;
		p.value = it->value;
		p.isTrailling = (it->type == TRAILING);
		parsedMessage.params.push_back(p);
		++it;	// WHEN WE CAN< WE SHOULD USE ++i FOR OPTIMIZING MEMORY (IT NEVER RETURNS A COPY OF THE LAST VALUE LIKE i++)
    }
	return parsedMessage;
}
