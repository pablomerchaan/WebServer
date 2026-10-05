NAME = webserver

SRCS = src/main.cpp src/Location.cpp src/Server.cpp src/ParseConfig.cpp

HEADERS	= inc/Webserver.hpp inc/ParseConfig.hpp inc/Server.hpp inc/Location.hpp

OBJS = $(SRCS:.cpp=.o)

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98
CXXFLAGS += -g3 
CXXFLAGS += -Iinc

RM = rm -rf

all:
	@$(MAKE) $(NAME)
$(NAME) : $(OBJS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

clean:
	$(RM) $(OBJS)

fclean: clean
	$(RM) $(NAME)

re: 	fclean all

.PHONY: all clean fclean re