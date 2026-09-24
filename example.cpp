#include <iostream>
#include <vector>
#include <string>
#include <sstream>

std::vector<std::string> tokenize(const std::string& raw_data) {
    std::vector<std::string> tokens;
    std::stringstream ss(raw_data);
    std::string token;
    size_t searchPos = 0;  // tracks where we are in raw_data

    while (ss >> token) {  // >> skips multiple spaces automatically
        if (token[0] == ':') {
            // find where THIS token starts in raw_data (not ss)
            size_t colonPos = raw_data.find(':', searchPos);

            // everything from colonPos+1 to end of raw_data is one token
            std::string trailing = raw_data.substr(colonPos);
            tokens.push_back(trailing);
            break;  // rest of line consumed, stop parsing
        }

        tokens.push_back(token);

        // advance searchPos past this token, so next find() doesn't rematch it
        searchPos = raw_data.find(token, searchPos) + token.length();
    }

    return tokens;
}

/*
0: [USER]
1: [nick]
2: [0]
3: [*]
4: [:Real Name Here]
*/
typedef struct generic_cmd
{
    std::string cmd_name;
    std::vector<string> cmd_args;
} generic_cmd;

generic_cmd parse_tokens(std::vector<string>& tokens)
{
    
    generic_cmd cd;
    if (tokens.empty())
    {
        cd = {};
        return cd;
    }
    cd.cmds_name = tokens[0];
    tokens.erase(tokens.begin())
    cd.cmd_args = tokens;
    return (cd);
}


int main() {
    std::string raw_data = "USER nick 0 * :Real Name Here";
    std::vector<std::string> result = tokenize(raw_data);

    for (std::vector<std::string>::size_type i = 0; i < result.size(); ++i) {
        std::cout << i << ": [" << result[i] << "]" << std::endl;
    }

    return 0;
}
