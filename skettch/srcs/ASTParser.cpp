#include "../headers/ASTParser.hpp"

// CONSTRUCTOR
ASTParser::ASTParser(const std::vector<Token>& tokens)
    : _tokens(tokens), _index(0) {}

// PRIVATE CURSOR HELPER FUNCTIONS
bool	ASTParser::isAtEnd() const
{
    return _index >= _tokens.size() || _tokens[_index].type == END_OF_LINE;
}

const Token& ASTParser::peekToken() const
{
    return _tokens[_index];
}

Token	ASTParser::advanceToken()
{
    if (isAtEnd())
    {
        Token tk;
        tk.type = END_OF_LINE;
        tk.value = "";
        return tk;
    }
    return _tokens[_index++];
}

// PRIVATE PARSING FUNCTIONS
void	ASTParser::parsePrefix(MessageAST& msg)
{
    if (!isAtEnd() && peekToken().type == PREFIX)
        msg.prefix = advanceToken().value;
}

void	ASTParser::parseCmd(MessageAST& msg)
{
    if (!isAtEnd() && peekToken().type == COMMAND)
        msg.command = advanceToken().value;
}

void	ASTParser::parseParams(MessageAST& msg)
{
    while (!isAtEnd())
    {
        Token tk = advanceToken();

        if (tk.type == PARAMETER || tk.type == TRAILING)
        {
            ParamNode* param = new ParamNode();
            param->value = tk.value;
            param->isTrailing = (tk.type == TRAILING);
            msg.params.push_back(param);
        }
    }
}

// PUBLIC FUNCTIONS
MessageAST* ASTParser::parse()
{
    MessageAST* messageAST = new MessageAST();

    parsePrefix(*messageAST);
    parseCmd(*messageAST);
    parseParams(*messageAST);
    
    return messageAST;
}