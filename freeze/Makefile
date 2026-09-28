CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Isrc -DDEFFILE=\"/etc/freeze.cnf\" -D_GNU_SOURCE
LDFLAGS ?=

SRC := src/bitio.c src/debug.c src/decode.c src/default.c src/encode.c src/freeze.c src/huf.c src/lz.c
OBJ := $(SRC:.c=.o)

BIN := freeze
TOOLS := statist showhuf

.PHONY: all clean test

all: $(BIN) $(TOOLS)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

statist: src/statist.o src/lz.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

showhuf: src/showhuf.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	./tests/run_tests.sh

clean:
	rm -f src/*.o $(BIN) $(TOOLS)
