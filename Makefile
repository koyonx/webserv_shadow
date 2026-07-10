NAME        := webserv

CXX         ?= c++
CXXFLAGS    := -Wall -Wextra -Werror -std=c++98 -pedantic
CPPFLAGS    := -Iinclude -MMD -MP
LDFLAGS     :=

SRC_DIR     := src
OBJ_DIR     := obj

SRCS        := $(shell find $(SRC_DIR) -type f -name '*.cpp' 2>/dev/null)
OBJS        := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS        := $(OBJS:.o=.d)

RM          := rm -f
MKDIR_P     := mkdir -p

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@$(MKDIR_P) $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	$(RM) -r $(OBJ_DIR)

fclean: clean
	$(RM) $(NAME)

re: fclean all

debug: CXXFLAGS += -g3 -O0 -DDEBUG
debug: re

sanitize: CXXFLAGS += -g3 -O0 -fsanitize=address,undefined
sanitize: LDFLAGS  += -fsanitize=address,undefined
sanitize: re

-include $(DEPS)

.PHONY: all clean fclean re debug sanitize
