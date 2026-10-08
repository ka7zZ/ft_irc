NAME		= ircserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98
CPPFLAGS	= -Iheaders

SOURCES		= main.cpp \
			sources/Channel.cpp \
			sources/Client.cpp \
			sources/Dispatcher.cpp \
			sources/IRCError.cpp \
			sources/IRCParser.cpp \
			sources/Server.cpp \
			sources/Commands/InviteCommand.cpp \
			sources/Commands/JoinCommand.cpp \
			sources/Commands/NickCommand.cpp \
			sources/Commands/PassCommand.cpp \
			sources/Commands/UserCommand.cpp
OBJECTS		:= $(SOURCES:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
