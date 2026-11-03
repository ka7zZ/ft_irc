NAME		= ircserv
BONUS_NAME	= bot
TESTER_NAME = irc_tester

CXX			= c++
CXXFLAGS	= -g -Wall -Wextra -Werror -std=c++98
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
			sources/Commands/KickCommand.cpp \
			sources/Commands/ListCommand.cpp \
			sources/Commands/ModeCommand.cpp \
			sources/Commands/NickCommand.cpp \
			sources/Commands/PartCommand.cpp \
			sources/Commands/PassCommand.cpp \
			sources/Commands/PingCommand.cpp \
			sources/Commands/PongCommand.cpp \
			sources/Commands/PrivmsgCommand.cpp \
			sources/Commands/TopicCommand.cpp \
			sources/Commands/UserCommand.cpp

BONUS_SOURCES	= bonus/main.cpp \
				bonus/Bot.cpp \
				bonus/dice.cpp \
				bonus/IRCParser.cpp

TEST_SOURCES	= ./tester/irc_tester.py

OBJECTS			= $(SOURCES:.cpp=.o)
BONUS_OBJECTS	= $(BONUS_SOURCES:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@

test:
	python3 $(TEST_SOURCES) --port 6667 --pass pass --pid $(pgrep -f valgrind.bin)

bonus: $(BONUS_NAME)

$(BONUS_NAME): $(BONUS_OBJECTS)
	$(CXX) $(CXXFLAGS) $(BONUS_OBJECTS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(OBJECTS:.o=.d)
-include $(BONUS_OBJECTS:.o=.d)

clean:
	rm -f $(OBJECTS) $(BONUS_OBJECTS)
	rm -f $(OBJECTS:.o=.d) $(BONUS_OBJECTS:.o=.d)

fclean: clean
	rm -f $(NAME) $(BONUS_NAME)

re: fclean all



.PHONY: all bonus clean fclean re
