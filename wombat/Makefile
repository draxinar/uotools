CC ?= cc
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Werror -O2
LDFLAGS ?=

ifdef ASAN
CFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
LDFLAGS += -fsanitize=address
endif

BIN = wombat

SRC = wombat.c main.c
HDR = wombat.h
OBJ = $(SRC:.c=.o)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

format:
	clang-format -i $(SRC) $(HDR)

test: $(BIN)
	./test.sh

.PHONY: all clean format test
