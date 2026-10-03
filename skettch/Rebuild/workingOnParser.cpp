#include <string>
#include <vector>



struct Token {
	std::string		content;
	TokenType		type;
};

size_t	orEnd(size_t pos, size_t length) {
	if (pos == std::string::npos)
		return length;
	return pos;
}

bool	endOfString(size_t idx, size_t max) {
	return idx >= max;
}

char	currentChar(const std::string& src, size_t idx) {
	if (idx >= src.length())
		return '\0';
	return src[idx];
}

size_t	skipWhitespaces(const std::string& src, size_t idx) {
	while (!endOfString(idx, src.length()) && currentChar(src, idx) == ' ') {
		idx++;
	}
	return idx;
}

Token	returnToken(TokenType type, const std::string& src, size_t start, size_t len) {
	Token	newToken;

	newToken.content = src.substr(start, len);
	newToken.type = type;
	return newToken;
}

std::vector<Token>	parsing(const std::string&  src) {
	std::vector<Token>	tokens;
	size_t				length = src.length();
	size_t				idx = skipWhitespaces(src, 0);
	size_t				end = 0;

	if (idx < length && currentChar(src, idx) == ':') {
		idx += 1;
		end = orEnd(src.find_first_of(" ", idx), length);
		if (end > idx)
			tokens.push_back(returnToken(PREFIX, src, idx, end - idx));
		idx = skipWhitespaces(src, end);
	}
	if (idx < length) {
		end = orEnd(src.find_first_of(" ", idx), length);
		if (end > idx)
			tokens.push_back(returnToken(COMMAND, src, idx, end - idx));
		idx = end;
	}
	while (idx < length) {
		idx = skipWhitespaces(src, idx);
		if (idx >= length)
			break ;
		if (currentChar(src, idx) == ':') {
			tokens.push_back(returnToken(TRAILING, src, ++idx, length - idx));
			break ;
		}
		end = orEnd(src.find_first_of(" ", idx), length);
		if (end > idx)
			tokens.push_back(returnToken(PARAMETER, src, idx, end - idx));
		idx = end;
	}
	tokens.push_back(returnToken(END_OF_LINE, src, length, 0));
	return	tokens;
}