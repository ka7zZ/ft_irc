#include "irc.hpp"

#include <sstream>

void handleError(Client& client,
		std::string error_label,
	       	std::vector<std::string> params)
{
    std::map<std::string, Error>::iterator it =
        table_errors.find(error_label);

    if (it == table_errors.end())
        return;

    std::ostringstream oss;
    oss << it->second.id;

    client.get_outbuffer() += ":server ";
    client.get_outbuffer() += oss.str();
    client.get_outbuffer() += " ";
    if (client.get_nickname().empty())
	    client.get_outbuffer() += "*";
    else
	    client.get_outbuffer() += client.get_nickname();
    client.get_outbuffer() += " ";

    for (size_t i = 0; i < params.size(); i++)
    {
        client.get_outbuffer() += params[i];

        if (i + 1 < params.size())
            client.get_outbuffer() += " ";
    }

    client.get_outbuffer() += ":";
    client.get_outbuffer() += it->second.msg;
    client.get_outbuffer() += "\r\n";
}

