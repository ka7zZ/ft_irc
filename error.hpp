#pragma once

#include "irc.hpp"

typedef struct s_Error
{
    size_t      id;
    std::string label;
    std::string msg;

    s_Error()
        : id(0), label(""), msg("")
    {
    }

    s_Error(size_t i, const std::string& l, const std::string& m)
        : id(i), label(l), msg(m)
    {
    }

} Error;
extern std::map<std::string, Error> table_errors;
void	handleError(Client& , std::string, std::vector<std::string>);

void generate_error_table();
