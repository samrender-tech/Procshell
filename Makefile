TARGET  = procshell
CC      = cc
CFLAGS  = -Wall -Wextra -g -Iinclude -D_XOPEN_SOURCE=700

# macOS hides SIGWINCH / TIOCGWINSZ / wait4 unless this is defined;
# Linux needs _DEFAULT_SOURCE for wait4
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  CFLAGS += -D_DARWIN_C_SOURCE
else
  CFLAGS += -D_DEFAULT_SOURCE
endif

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build

fclean: clean
	rm -f $(TARGET)

re: fclean all

.PHONY: all clean fclean re
