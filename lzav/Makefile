CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c99 -Isrc

all: lzav

lzav: src/lzav_main.c src/lzav.h
	$(CC) $(CFLAGS) -o $@ src/lzav_main.c

test: lzav
	@bash tests/run_tests.sh

clean:
	rm -f lzav *.o tests/test_*

.PHONY: all test clean
