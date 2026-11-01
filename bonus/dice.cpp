#include "dice.hpp"

std::string	Dice::getParticipant() const 
{
	size_t	pos = _prefix.find("!"); // 
	return (_prefix.substr(0, pos));
}

// :kil
std::string Dice::usageDice() const
{

	std::string usage = getParticipant();
	usage += ": Invalid syntax. Use !help for usage.";
	return usage;
}

std::string Dice::limitsDice() const
{
	std::string limits = getParticipant();
	limits += ": Limit exceeded. Use !help for usage.";
	return limits;
}



Dice::Dice(const std::string &arg, const std::string& prefix)
:dice_count(0), dice_faces(0), total(0), argsToProcess(arg), _prefix(prefix)
{
}


bool	Dice::extractValues()
{


	int len = argsToProcess.length();
	if (std::count(argsToProcess.begin(), argsToProcess.end(), ' ') >= 1 && argsToProcess.find_first_not_of(' ') == std::string::npos)
		return (false);
	if (std::count(argsToProcess.begin(), argsToProcess.end(), ' ') > 1 && argsToProcess.find_first_not_of(' ') != std::string::npos)
		return (false);
	if (argsToProcess.find("d") != std::string::npos)
	{
		if (len < 2)
			return (false);
		if (len > 2)
		{
			std::stringstream ss(argsToProcess);
			ss >> dice_count;
			if (ss.fail())
				return (false);
			ss >> dice;
			if (dice != 'd')
				return (false);
			ss >> dice_faces;
			if (ss.fail())
				return (false);
		}

	}
	else
	{
		if (len > 2)
			return (false);
		if (len == 0)
		{
			dice_count = 1;
			dice = 'd';
			dice_faces = 20;
		}	
	}
	return (true);
}

bool	Dice::validateRangeValues()
{
	if (dice_count < 1 || dice_count > 10)
		return (false);
	if (dice_faces < 1 || dice_faces > 20)
		return (false);
	return (true);
}

// PRIVMSG #foot :kil: 2d6: [3, 5] = 8
int	Dice::sumValues()
{
	for (size_t i = 0 ; i < generated_values.size(); i++)
	{
		total += generated_values[i];
	}
	return (total);
}

std::string	Dice::resultDice()
{
	std::stringstream ss;
	ss << getParticipant();
	ss << ":";
	if (argsToProcess.empty())
		ss << "1d20";
	else
	{
		size_t pos = argsToProcess.find_first_not_of(' ');
		ss << argsToProcess.substr(pos);
	}
	ss << ": [ ";
	for (size_t i = 0; i < generated_values.size(); i++)
	{

		ss << generated_values[i] << " ";
	}
	ss << "] = " << total;
	return ss.str();      
}

void	Dice::runDice()
{
	srand(time(NULL));
	int i  = dice_count;
	while (i--)
	{
		int answer = 1 + rand() %  dice_faces;
		generated_values.push_back(answer);
	}

}

