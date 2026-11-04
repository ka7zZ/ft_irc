# include "Dice.hpp"
# include <algorithm>
# include <sstream>

Dice::Dice(const std::string &arg, const std::string& prefix)
	:_count(0),
	_faces(0),
	_total(0),
	_args(arg),
	_prefix(prefix) {}

Dice::~Dice() {}

const std::string&	Dice::getArgs() const { return _args; }

const std::string	Dice::getParticipant() const 
{
	size_t	pos = _prefix.find("!"); // 
	return pos == std::string::npos ? _prefix : _prefix.substr(0, pos);
}

const std::string Dice::invalidSyntax() const
{
	return getParticipant() + ": Invalid syntax. Type !help for more information.";
}

const std::string Dice::limitExceeded() const
{
	return getParticipant() + ": Limit exceeded. Type !help for more information.";
}

bool	Dice::extractValues()
{
	int len = _args.length();
	std::stringstream ss(_args);

	if (len < 2)
		return (false);
	if (std::count(_args.begin(), _args.end(), ' ') >= 1 && 
		_args.find_first_not_of(' ') == std::string::npos)
		return (false);
	if (std::count(_args.begin(), _args.end(), ' ') > 1 
		&& _args.find_first_not_of(' ') != std::string::npos)
		return (false);
	if (_args.find("d") == std::string::npos)
		return (false);
	ss >> _count;
	if (ss.fail())
		return (false);
	ss >> _dice;
	if (_dice != 'd')
		return (false);
	ss >> _faces;
	if (ss.fail())
		return (false);
	return (true);
}

bool	Dice::validateRangeValues()
{
	return _count > 0 && _count <= 10 && _faces > 0 && _faces <= 20;
}

int	Dice::sumValues()
{
	for (size_t i = 0 ; i < _result.size(); i++)
	{
		_total += _result[i];
	}
	return (_total);
}

const std::string	Dice::resultDice()
{
	std::stringstream ss;

	ss << getParticipant();
	ss << ":";
	if (_args.empty())
		ss << "1d20";
	else
	{
		size_t pos = _args.find_first_not_of(' ');
		ss << _args.substr(pos);
	}
	ss << ": [ ";
	for (size_t i = 0; i < _result.size(); i++)
	{
		ss << _result[i] << " ";
	}
	ss << "] = " << _total;
	return ss.str();      
}

void	Dice::runDice()
{
	srand(time(NULL));
	int i  = _count;
	while (i--)
	{
		int answer = 1 + rand() %  _faces;
		_result.push_back(answer);
	}
}

