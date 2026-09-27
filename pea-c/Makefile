CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDLIBS  ?= -lz -lcrypto -lnettle

SRC := src/pea_digest.c src/pea_archive.c src/pea_eax.c src/pea_vol.c src/pea_block.c
OBJ := $(SRC:.c=.o)

BIN := pea

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ) src/pea_cli.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c src/pea.h src/pea_digest.h src/pea_archive.h src/pea_eax.h src/pea_vol.h src/pea_block.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(BIN)
	./tests/run_tests.sh

clean:
	rm -f $(OBJ) src/pea_cli.o $(BIN)
