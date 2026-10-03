#pragma once

#include "irc.hpp"

typedef struct generic_cmd
{
    std::string cmd_name;
    std::vector<std::string> cmd_args;
} generic_cmd;


class Parser
{
	public:
		static std::vector<std::string> tokenize(const std::string&); 
		static generic_cmd parse_tokens(std::vector<std::string>&);
		static generic_cmd parser(const std::string&);
};
