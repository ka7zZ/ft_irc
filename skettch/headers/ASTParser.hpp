#ifndef ASTPARSER_HPP
# define ASTPARSER_HPP

# include <string>
# include <vector>
# include "Token.hpp"

// AST nodes
struct	ASTNode {
	virtual ~ASTNode() {};
};

struct ParamNode : public ASTNode {
	std::string	value;
	bool		isTrailing;
    virtual ~ParamNode() {}
};

struct MessageAST: public ASTNode {
	std::string				prefix;
	std::string				command;
	std::vector<ParamNode*>	params;

	virtual ~MessageAST()
	{
		for (size_t i = 0; i < params.size(); i++)
		{
			delete params[i];
		}
	}
};

class ASTParser
{
	private:
		std::vector<Token>	_tokens;
		size_t				_index;

		bool			isAtEnd() const;
		const Token&	peekToken() const;
		Token			advanceToken();
		void			parsePrefix(MessageAST& msg);
		void			parseCmd(MessageAST& msg);
		void			parseParams(MessageAST& msg);

	public:
		explicit ASTParser(const std::vector<Token>& tokens);

		MessageAST*	parse();
};

#endif
