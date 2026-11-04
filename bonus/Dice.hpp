#ifndef DICE_HPP
# define DICE_HPP

#include <string>
#include <vector>

class Dice
{
	private:
		char				_dice;
		int					_count;
		int					_faces;
		int					_total;
		std::string			_args;
		std::string			_prefix;
		std::vector<int>	_result;

	public:
		Dice(const std::string &arg, const std::string& prefix);
		~Dice();

		const std::string&	getArgs() const;

		const std::string	limitExceeded() const ;
		const std::string	invalidSyntax() const ;
		const std::string	getParticipant() const ;

		bool	extractValues();
		bool	validateRangeValues();

		int		sumValues();
		void	runDice();

		const std::string	resultDice();
};

#endif
