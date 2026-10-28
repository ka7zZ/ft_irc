#pragma once

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

class Dice
{
	private:
		int	dice_count;
		int	dice_faces;
		char	dice;
		int	total;
		std::string argsToProcess;  // "!roll " or "!roll 2d6" or "!roll"
		std::string _prefix;
		std::vector<int> generated_values;

	public:
		Dice(const std::string &arg, const std::string& prefix);
		~Dice() {};
		
		
		bool	extractValues();
		bool	validateRangeValues();

		std::string	limitsDice() const ;
		std::string	usageDice() const ;
		std::string	getParticipant() const ;

		int	sumValues();

		void	runDice();
		
		std::string	resultDice();
};
