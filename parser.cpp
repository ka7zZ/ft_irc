#include "irc.hpp"


std::vector<std::string> Parser::tokenize(const std::string& raw_data) 
{
	std::vector<std::string> tokens;
	std::stringstream ss(raw_data);
	std::string token;
	size_t searchPos = 0;  // tracks where we are in raw_data

	while (ss >> token)   // >> skips multiple spaces automatically             JOIN  #football\r\n
	{                                                                    //            |
		if (token[0] == ':') 
		{
            		// find where THIS token starts in raw_data (not ss)
            		size_t colonPos = raw_data.find(':', searchPos);

            		// everything from colonPos+1 to end of raw_data is one token
           	 	std::string trailing = raw_data.substr(colonPos);
		 	tokens.push_back(trailing);
           	 	break;  // rest of line consumed, stop parsing
		}
        
		tokens.push_back(token);        //  [JOIN, ]

		// advance searchPos past this token, so next find() doesn't rematch it
		searchPos = raw_data.find(token, searchPos) + token.length();  // searchPos = 4;
	}
	return (tokens);
}



generic_cmd Parser::parse_tokens(std::vector<std::string>& tokens)
{

    generic_cmd cd;
    if (tokens.empty())
    {
        cd = generic_cmd();
        return cd;
    }
    cd.cmd_name = tokens[0];
    tokens.erase(tokens.begin());
    cd.cmd_args = tokens;
    return (cd);
}

generic_cmd Parser::parser(const std::string& raw_data)
{
	std::vector<std::string> t = tokenize(raw_data);
	generic_cmd g_cmd = parse_tokens(t);
	return (g_cmd);
}
