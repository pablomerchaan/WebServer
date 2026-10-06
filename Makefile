NAME = webserv

SRCS = src/main.cpp src/Location.cpp src/Server.cpp src/ParseConfig.cpp \
       src/Utils.cpp src/HttpRequest.cpp src/HttpResponse.cpp src/Cgi.cpp \
       src/RequestHandler.cpp src/Client.cpp src/ServerManager.cpp

HEADERS = inc/Webserver.hpp inc/ParseConfig.hpp inc/Server.hpp inc/Location.hpp \
          inc/Utils.hpp inc/HttpRequest.hpp inc/HttpResponse.hpp inc/Cgi.hpp \
          inc/RequestHandler.hpp inc/Client.hpp inc/ServerManager.hpp

OBJS = $(SRCS:.cpp=.o)

CXX = c++

CXXFLAGS = -Wall -Wextra -Werror -std=c++98
CXXFLAGS += -g3
CXXFLAGS += -Iinc

RM = rm -rf

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

# Cada .o depende de su .cpp y de los headers: si cambia un .hpp se recompila
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS)

fclean: clean
	$(RM) $(NAME)

re: fclean all

.PHONY: all clean fclean re
